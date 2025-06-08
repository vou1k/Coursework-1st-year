    #include <SFML/Graphics.hpp>
    #include <SFML/System.hpp>
    #include <SFML/Window.hpp>
    #include <SFML/Graphics/Text.hpp>
    #include <iostream>
    #include <queue>
    #include <vector>
    #include <thread>
    #include <mutex>
    #include <condition_variable>
    #include <cstdlib>
    #include <ctime>
    #include <random>
    #include <atomic>

    using namespace std;
    std::atomic<double> lambdaRate(1.0); // начальное значение λ
    std::atomic<double> muRate(0.5); // начальное значение μ (0.5 означает в среднем 2 секунды на клиента)
    std::atomic<int> clientsServed{0};
    std::atomic<double> totalServiceTime{0.0};  // в секундах
    std::atomic<bool> simulationRunning{true};
    mutex intervalsMutex;
    deque<double> timeIntervals;
    mutex queueMutex;
    mutex visualsMutex;

    sf::RectangleShape createButton(float x, float y, float width, float height, sf::Color color) {
        sf::RectangleShape button(sf::Vector2f(width, height));
        button.setPosition(x, y);
        button.setFillColor(color);
        button.setOutlineThickness(2);
        button.setOutlineColor(sf::Color::White);
        return button;
    }

    sf::Text createText(const sf::Font& font, const string& str, float x, float y, int size, sf::Color color) {
        sf::Text text(str, font, size);
        text.setPosition(x, y);
        text.setFillColor(color);
        return text;
    }
    condition_variable cv;
    bool bankOpen = true;

    // Клиент банка
    struct Client {
        int id;
        string operation;
        int amount;
        sf::RectangleShape shape;


        Client() : id(0), operation(""), amount(0.0f), shape(sf::Vector2f(40, 40)) {}


        Client(int id_, const std::string& op, float amt)
            : id(id_), operation(op), amount(amt), shape(sf::Vector2f(40, 40)) {}
    };
    struct Statistics {
        int totalClients = 0;
        int deposits = 0;
        int withdrawals = 0;
        int loans = 0;
        int inquiries = 0;
        double totalServiceTime = 0.0;
        double totalWaitTime = 0.0;
        mutex statsMutex;
        void reset() {
            lock_guard<mutex> lock(statsMutex);
            totalClients = 0;
            deposits = 0;
            withdrawals = 0;
            loans = 0;
            inquiries = 0;
            totalServiceTime = 0.0;
            totalWaitTime = 0.0;
        }
    };

    queue<Client> bankQueue;
    vector<Client> processedClients;

    Statistics stats;
    // Генерация случайной операции
    string getRandomOperation() {
        string operations[] = {"Deposit", "Withdraw", "Loan", "Inquiry"};
        return operations[rand() % 4];
    }

    // Генерация случайного клиента
    Client generateClient(int id) {
        return Client(id, getRandomOperation(), rand() % 1000 + 1);
    }

    // Обработка клиента
    void processClient(int workerId, vector<string>& logs, mutex& logsMutex,
                   vector<sf::RectangleShape>& visuals, mutex& visualsMutex) {
    std::default_random_engine generator(std::random_device{}());

    while (true) {
        if (!simulationRunning.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        Client client;
        {
            unique_lock<mutex> lock(queueMutex);
            if (bankQueue.empty()) {
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                continue;
            }
            client = bankQueue.front();
            bankQueue.pop();
        }

        // Переместить клиента к кассе визуально
        {
            lock_guard<mutex> visualLock(visualsMutex);
            client.shape.setPosition(620 + (workerId % 3) * 100, 200 + (workerId / 3) * 100);
            visuals[workerId].setFillColor(sf::Color::Blue); // Работает
        }

        // Лог начала обслуживания
        {
            lock_guard<mutex> logLock(logsMutex);
            logs.push_back("Worker " + to_string(workerId) + " started processing client " +
                           to_string(client.id) + " - " + client.operation +
                           " ($" + to_string(client.amount) + ")");
        }

        // Генерация времени обслуживания и ожидание
        std::exponential_distribution<double> exp_dist(muRate.load());
        double serviceTime = exp_dist(generator);

        auto startTime = std::chrono::high_resolution_clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(serviceTime * 1000)));
        auto endTime = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = endTime - startTime;

        {
            lock_guard<mutex> visualLock(visualsMutex);
            visuals[workerId].setFillColor(sf::Color::Yellow); // Свободен
        }

        // Обновление статистики
        {
            lock_guard<mutex> statsLock(stats.statsMutex);
            stats.totalClients++;
            stats.totalServiceTime += elapsed.count();
            if (client.operation == "Deposit") stats.deposits++;
            else if (client.operation == "Withdraw") stats.withdrawals++;
            else if (client.operation == "Loan") stats.loans++;
            else if (client.operation == "Inquiry") stats.inquiries++;
        }

        // Лог завершения
        {
            lock_guard<mutex> logLock(logsMutex);
            logs.push_back("Worker " + to_string(workerId) + " finished client " +
                           to_string(client.id) + " in " + to_string(elapsed.count()) + " sec");
        }

        {
            lock_guard<mutex> lock(queueMutex);
            processedClients.push_back(client);
        }
    }
}

    void poissonClientGenerator(int& clientId, mutex& logsMutex, vector<string>& logs,
                              deque<double>& timeIntervals, mutex& intervalsMutex) {

        std::default_random_engine generator(static_cast<unsigned>(time(0)));
        std::exponential_distribution<double> exp_dist(lambdaRate.load());
        double interval = exp_dist(generator);
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
        auto lastTime = chrono::steady_clock::now();  // замер времени

        while (bankOpen) {
            double interval = exp_dist(generator); // случайный интервал
            std::this_thread::sleep_for(std::chrono::milliseconds(int(interval * 1000)));
            exp_dist = std::exponential_distribution<double>(lambdaRate.load());
            //Расчет реального интервала
            auto now = chrono::steady_clock::now();
            chrono::duration<double> actualInterval = now - lastTime;
            lastTime = now;
            // Сохраняем статистику
            {
                lock_guard<mutex> lock(intervalsMutex);
                timeIntervals.push_back(actualInterval.count());
                if (timeIntervals.size() > 20) timeIntervals.pop_front();  // Храним последние 20 интервалов
            }
            {
                lock_guard<mutex> statsLock(stats.statsMutex);
                stats.totalWaitTime += actualInterval.count();
            }
            Client newClient = generateClient(clientId++);

            {
                lock_guard<mutex> lock(queueMutex);
                bankQueue.push(newClient);
            }
            cv.notify_one();

            {
                lock_guard<mutex> logLock(logsMutex);
                logs.push_back("New client (Poisson) arrived: " + to_string(newClient.id) +
                              " - " + newClient.operation + " ($" + to_string(newClient.amount) +
                              ") | Interval: " + to_string(actualInterval.count()).substr(0, 5) + "s");
            }

        }
    }
void drawStatsWindow(sf::RenderWindow& statsWindow, const Statistics& stats) {
        statsWindow.clear(sf::Color(50, 50, 70));

        sf::Font font;
        if (!font.loadFromFile("C:/Users/1/CLionProjects/HELLOSFML/fonts/arial/arial.ttf")) {
            return;
        }

        sf::Text statsText;
        statsText.setFont(font);
        statsText.setCharacterSize(18);
        statsText.setFillColor(sf::Color::White);

        float yPos = 20.f;
        auto addText = [&](const string& text) {
            statsText.setString(text);
            statsText.setPosition(20.f, yPos);
            statsWindow.draw(statsText);
            yPos += 25.f;

        };

        addText("=== Bank Statistics ===");
        addText("Total clients: " + to_string(stats.totalClients));
        addText("Deposits: " + to_string(stats.deposits));
        addText("Withdrawals: " + to_string(stats.withdrawals));
        addText("Loans: " + to_string(stats.loans));
        addText("Inquiries: " + to_string(stats.inquiries));

        double avgService = stats.totalClients > 0 ? stats.totalServiceTime / stats.totalClients : 0;
        double avgWait = stats.totalClients > 0 ? stats.totalWaitTime / stats.totalClients : 0;

        addText("Avg service time: " + to_string(avgService).substr(0, 4) + "s");
        addText("Avg wait time: " + to_string(avgWait).substr(0, 4) + "s");

        statsWindow.display();
    }

    int main() {
        srand(time(0));
        int clientId = 1;
        const int numWorkers = 5;
        vector<sf::RectangleShape> workersVisuals;
        vector<thread> workers;
        vector<string> logs;
        mutex logsMutex;
        bool simulationStarted = false;
        thread poissonGenerator;
        sf::RenderWindow window(sf::VideoMode(1000, 500), "Bank Simulator");
        sf::Font font;
        if (!font.loadFromFile("C:/Users/1/CLionProjects/HELLOSFML/fonts/arial/arial.ttf")) {
            cerr << "Error loading font!" << endl;
            return -1;
        }
        const size_t MAX_LOG_LINES = 100;  // Максимальное количество хранимых логов
        const float LOG_START_Y = 15.f;    // Начальная позиция Y для первого лога
        const float LINE_HEIGHT = 20.f;    // Высота строки лога

        for (int i = 0; i < numWorkers; ++i) {
            sf::RectangleShape worker(sf::Vector2f(40, 40));
            worker.setFillColor(sf::Color::Yellow);
            sf::Text label("Cashier " + to_string(i+1), font, 16);
            label.setFillColor(sf::Color::White);
            label.setPosition(worker.getPosition().x, worker.getPosition().y - 20);
            window.draw(label);
            worker.setPosition(620 + (i % 3) * 100, 100 + (i / 3) * 100); // 3 в строке, остальные ниже
            workersVisuals.push_back(worker);
        }
        sf::RectangleShape logBackground(sf::Vector2f(590, 450));
        logBackground.setPosition(5, 5);
        logBackground.setFillColor(sf::Color(30, 30, 30)); //
        logBackground.setOutlineColor(sf::Color::White);
        logBackground.setOutlineThickness(1);
        // Текст для логов
        sf::Text logText("", font, 14);
        logText.setFillColor(sf::Color::White);

        // Левый — для логов
        sf::View logView(sf::FloatRect(0, 0, 600, 500));
        logView.setViewport(sf::FloatRect(0.f, 0.f, 0.6f, 1.f));  // 60% ширины

        // Правый — для визуальной части
        sf::View visualView(sf::FloatRect(0, 0, 400, 500));
        visualView.setViewport(sf::FloatRect(0.6f, 0.f, 0.4f, 1.f));  // 40% ширины
        // Кнопки
        auto startButton = createButton(620, 400, 120, 40, sf::Color::Green);
        auto startText = createText(font, "Start", 650, 410, 18, sf::Color::Black);

        sf::RectangleShape stopButton(sf::Vector2f(120, 40));
        stopButton.setPosition(770, 400);
        stopButton.setFillColor(sf::Color::Red);
        auto increaseMuButton = createButton(790, 340, 40, 30, sf::Color::White);
        sf::Text increaseMuText = createText(font, "+", 805, 340, 20, sf::Color::Black);
        sf::Text muText = createText(font, "mu = 0.5", 790, 310, 20, sf::Color::Cyan);
        auto decreaseMuButton = createButton(840, 340, 40, 30, sf::Color::White);
        sf::Text decreaseMuText = createText(font, "-", 855, 340, 20, sf::Color::Black);

        sf::RectangleShape increaseLambdaButton = createButton(620, 340, 40, 30, sf::Color::White);
        sf::Text increaseText = createText(font, "+", 635, 340, 20, sf::Color::Black);
        sf::Text lambdaText = createText(font, "lambda = 1.0", 620, 310, 20, sf::Color::Magenta);
        sf::RectangleShape decreaseLambdaButton = createButton(670, 340, 40, 30, sf::Color::White);
        sf::Text decreaseText = createText(font, "-", 685, 340, 20, sf::Color::Black);

        sf::Text stopText("Stop", font, 18);
        stopText.setPosition(800, 410);
        stopText.setFillColor(sf::Color::Black);
        sf::RectangleShape timeline(sf::Vector2f(200, 10));
        timeline.setPosition(550, 380);
        timeline.setFillColor(sf::Color::Blue);

        sf::RectangleShape resetButton(sf::Vector2f(120, 40));
        resetButton.setPosition(770, 450);  // Под  Stop
        resetButton.setFillColor(sf::Color(100, 100, 255));
        sf::Text resetText("Reset", font, 18);
        resetText.setPosition(800, 460);
        resetText.setFillColor(sf::Color::White);

        // Индикатор очереди - фон
        sf::RectangleShape queueIndicatorBg(sf::Vector2f(120, 30));
        queueIndicatorBg.setPosition(620, 450);  // Под кнопками
        queueIndicatorBg.setFillColor(sf::Color(50, 50, 50));
        queueIndicatorBg.setOutlineThickness(1);
        queueIndicatorBg.setOutlineColor(sf::Color::White);

        // Текст индикатора очереди
        sf::Text queueSizeText("Queue: 0", font, 18);
        queueSizeText.setPosition(630, 453);  // Смещение внутри фона
        queueSizeText.setFillColor(sf::Color::White);
        sf::Text statsText("", font, 16);
        statsText.setPosition(550, 360);
        statsText.setFillColor(sf::Color::White);
        lambdaText.setString("lambda = " + std::to_string(lambdaRate.load()).substr(0, 4));
        {
            lock_guard<mutex> lock(intervalsMutex);
            if (!timeIntervals.empty()) {
                double avg = accumulate(timeIntervals.begin(), timeIntervals.end(), 0.0) / timeIntervals.size();
                statsText.setString(
                "Poisson λ=" + std::to_string(lambdaRate.load()).substr(0, 4) + "\n" +
                "Avg: " + std::to_string(avg).substr(0, 4) + "s\n" +
                "Last: " + std::to_string(timeIntervals.back()).substr(0, 4) + "s\n" +
                "Clients: " + std::to_string(clientId - 1)
                );
            }
        }
        window.draw(statsText);
        {
            lock_guard<mutex> lock(queueMutex);
            for (const auto& client : processedClients) {
                window.draw(client.shape);
            }
        }
        deque<double> timeIntervals; // Для хранения интервалов
        // Переменные для прокрутки
        float scrollOffset = 0.0f;
        const float scrollSpeed = 20.0f; // Скорость прокрутки
        const float maxScrollOffset = 1000.0f; // Максимальное смещение

        // Создаем вид (View) для текстовой области
        sf::View textView(sf::FloatRect(10, 10, 580, 330)); // Область текста
        textView.setViewport(sf::FloatRect(0.0f, 0.0f, 0.6f, 0.9f)); // 80% окна для текста
        sf::RectangleShape separator(sf::Vector2f(2, 500));
        separator.setPosition(600, 0); // граница между логами и визуализацией
        separator.setFillColor(sf::Color::White);
        sf::RenderWindow statsWindow(sf::VideoMode(400, 300), "Bank Statistics");
        statsWindow.setPosition(sf::Vector2i(1000, 0));
        while (window.isOpen()) {
            window.clear();
            sf::Event event;
            while (window.pollEvent(event)) {
                if (event.type == sf::Event::Closed)
                    window.close();

                if (event.type == sf::Event::MouseButtonPressed) {
                    if (event.mouseButton.button == sf::Mouse::Left) {
                        sf::Vector2i mousePos = sf::Mouse::getPosition(window);
                        if (increaseMuButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            muRate.store(std::min(2.0, muRate.load() + 0.1));
                            logs.push_back("mu increased to " + std::to_string(muRate.load()).substr(0, 4));
                        }
                        else if (decreaseMuButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            muRate.store(std::max(0.1, muRate.load() - 0.1));
                            logs.push_back("mu decreased to " + std::to_string(muRate.load()).substr(0, 4));
                        }
                        if (startButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            if (!simulationStarted) {
                                simulationStarted = true;

                                poissonGenerator = thread(poissonClientGenerator, ref(clientId), ref(logsMutex),
                                                          ref(logs), ref(timeIntervals), ref(intervalsMutex));

                                for (int i = 0; i < numWorkers; i++) {
                                    workers.emplace_back(processClient, i, ref(logs), ref(logsMutex),
                                                         ref(workersVisuals), ref(visualsMutex));
                                }

                                {
                                    lock_guard<mutex> logLock(logsMutex);
                                    logs.push_back("Simulation started.");
                                }

                            }

                        }

                        if (increaseLambdaButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            lambdaRate.store(std::min(10.0, lambdaRate.load() + 0.1));
                            logs.push_back("lambda increased to " + std::to_string(lambdaRate.load()).substr(0, 4));
                            cout << "New lambda value: " << lambdaRate.load() << endl;
                        }
                        else if (decreaseLambdaButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            lambdaRate.store(std::max(0.1, lambdaRate.load() - 0.1));
                            logs.push_back("lambda decreased to " + std::to_string(lambdaRate.load()).substr(0, 4));
                            cout << "New lambda value: " << lambdaRate.load() << endl;
                            if (!simulationStarted) {
                                simulationStarted = true;


                                // Запуск генератора клиентов
                                poissonGenerator = thread(poissonClientGenerator, ref(clientId), ref(logsMutex),
                                                          ref(logs), ref(timeIntervals), ref(intervalsMutex));

                                // Запуск потоков сотрудников
                                for (int i = 0; i < numWorkers; i++) {
                                    workers.emplace_back(processClient, i, ref(logs), ref(logsMutex),
                                                         ref(workersVisuals), ref(visualsMutex));
                                }

                                {
                                    lock_guard<mutex> logLock(logsMutex);
                                    logs.push_back("Simulation started.");
                                }
                            }


                            for (int i = 0; i < 10; i++) {
                                Client newClient = generateClient(clientId++);

                                {
                                    lock_guard<mutex> lock(queueMutex);
                                    bankQueue.push(newClient);
                                }
                                cv.notify_one();

                                {
                                    lock_guard<mutex> logLock(logsMutex);
                                    logs.push_back("New client arrived: " + to_string(newClient.id) + " - " +
                                                  newClient.operation + " ($" + to_string(newClient.amount) + ")");
                                }
                            }
                        }

                        if (stopButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            {
                                lock_guard<mutex> lock(queueMutex);
                                bankOpen = false;
                            }
                            cv.notify_all();
                        }
                        if (resetButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
                            // Шаг 1: Остановить все потоки
                            {
                                lock_guard<mutex> lock(queueMutex);
                                bankOpen = false; // Говорим потокам завершиться
                            }
                            cv.notify_all(); // Будим все потоки

                            // Шаг 2: Дождаться завершения потоков
                            if (poissonGenerator.joinable()) {
                                poissonGenerator.join();
                            }

                            for (auto& worker : workers) {
                                if (worker.joinable()) {
                                    worker.join();
                                }
                            }

                            // Шаг 3: Очистить векторы потоков
                            workers.clear();

                            // Шаг 4: Сбросить состояние симуляции
                            {
                                lock_guard<mutex> lock(queueMutex);
                                bankQueue = queue<Client>(); // Очищаем очередь
                                processedClients.clear();
                                bankOpen = true; // Возвращаем в исходное состояние
                            }

                            // Сброс статистики
                            {
                                lock_guard<mutex> lock(stats.statsMutex);
                                stats.reset(); // Сбрасываем всю статистику
                            }

                            {
                                lock_guard<mutex> lock(logsMutex);
                                logs.clear();
                                logs.push_back("Simulation reset. Ready to start again.");
                            }

                            {
                                lock_guard<mutex> lock(intervalsMutex);
                                timeIntervals.clear();
                            }

                            // Шаг 5: Сбросить визуальные элементы
                            {
                                lock_guard<mutex> visualLock(visualsMutex);
                                for (auto& worker : workersVisuals) {
                                    worker.setFillColor(sf::Color::Yellow); // Возвращаем в "свободен"
                                }
                            }

                            // Шаг 6: Сбросить ID клиентов и флаг симуляции
                            clientId = 1;
                            simulationStarted = false;
                        }
                    }
                }


                // Обработка прокрутки колеса мыши
                if (event.type == sf::Event::MouseWheelScrolled) {
                    if (event.mouseWheelScroll.wheel == sf::Mouse::VerticalWheel) {
                        scrollOffset += event.mouseWheelScroll.delta * scrollSpeed;
                        scrollOffset = max(0.0f, min(scrollOffset, maxScrollOffset)); // Ограничение смещения
                        textView.setCenter(textView.getCenter().x, textView.getSize().y / 2 + scrollOffset);
                    }
                }
            }



            // Устанавливаем вид для текста
            window.setView(textView);


            lambdaText.setString("lambda = " + std::to_string(lambdaRate.load()).substr(0, 4));
            // Отрисовка логов
            window.setView(logView);
            window.draw(logBackground);

            {
                lock_guard<mutex> logLock(logsMutex);

                // Обрезаем логи если их слишком много
                if (logs.size() > MAX_LOG_LINES) {
                    logs.erase(logs.begin(), logs.begin() + (logs.size() - MAX_LOG_LINES));
                }

                float currentY = LOG_START_Y - scrollOffset;

                for (const auto& log : logs) {
                    logText.setString(log);
                    logText.setPosition(15.f, currentY);
                    window.draw(logText);
                    currentY += LINE_HEIGHT;

                    // Прекращаем отрисовку если текст уходит за границы
                    if (currentY > logBackground.getSize().y) break;
                }
            }
            lambdaText.setString("lambda = " + std::to_string(lambdaRate.load()).substr(0, 4));

            // Возвращаем вид по умолчанию для отрисовки кнопок
            window.setView(window.getDefaultView());

            // Отрисовка кнопок
            window.draw(startButton);
            window.draw(resetButton);
            window.draw(resetText);
            window.draw(startText);
            window.draw(stopButton);
            window.draw(stopText);

            window.draw(increaseLambdaButton);
            window.draw(increaseText);
            window.draw(lambdaText);
            window.draw(decreaseLambdaButton);
            window.draw(decreaseText);

            muText.setString("mu = " + std::to_string(muRate.load()).substr(0, 4));
            window.draw(increaseMuButton);
            window.draw(increaseMuText);
            window.draw(muText);
            window.draw(decreaseMuButton);
            window.draw(decreaseMuText);

            {
                lock_guard<mutex> visualLock(visualsMutex);
                for (const auto& worker : workersVisuals) {
                    window.draw(worker);
                }
            }
            // Обновление и отрисовка индикатора очереди
            {
                lock_guard<mutex> lock(queueMutex);
                queueSizeText.setString("Queue: " + to_string(bankQueue.size()));
            }

            window.draw(queueIndicatorBg);
            window.draw(queueSizeText);
            // Отрисовка окна статистики
            {
                lock_guard<mutex> statsLock(stats.statsMutex);
                drawStatsWindow(statsWindow, stats);
            }
            window.display();
        }

        for (auto& worker : workers) {
            worker.join();
        }
        if (poissonGenerator.joinable())
            poissonGenerator.join();
        for (auto& worker : workers) {
            if (worker.joinable())
                worker.join();
        }
        if (poissonGenerator.joinable())
            poissonGenerator.join();
        statsWindow.close();
        return 0;
    }