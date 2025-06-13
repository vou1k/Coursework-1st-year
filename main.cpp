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
    std::atomic<bool> resetRequested{false};
    //const int numWorkers = 5;
    std::atomic<int> numWorkers(3); // Начальное значение - 3 кассы

    mutex intervalsMutex;
    deque<double> timeIntervals;
    mutex queueMutex;
    mutex visualsMutex;
    // Назначение операций на кассы
    vector<string> workerSpecialization = {
        "Deposit", "Withdraw", "Loan", "Inquiry", "Any"
    };

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
        // заработок по операциям
        double depositProfit = 0.0;
        double withdrawalProfit = 0.0;
        double loanProfit = 0.0;
        double inquiryProfit = 0.0;
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
            depositProfit = 0.0;
            withdrawalProfit = 0.0;
            loanProfit = 0.0;
            inquiryProfit = 0.0;
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
        if (resetRequested.load()) {
            break;
        }
        if (!simulationRunning.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (resetRequested.load() || !simulationRunning.load()) break;
            continue;
        }

        Client client;
        bool clientFound = false;
        {
            unique_lock<mutex> lock(queueMutex);
            cv.wait(lock, [] { return !bankQueue.empty() || !bankOpen || resetRequested.load(); });
            if (resetRequested.load() || !bankOpen) break;
            if (bankQueue.empty()) continue;

            // Поиск подходящего клиента
            queue<Client> tempQueue;
            while (!bankQueue.empty()) {
                Client potential = bankQueue.front();
                bankQueue.pop();

                if (workerSpecialization[workerId] == "Any" ||
                    potential.operation == workerSpecialization[workerId]) {
                    client = potential;
                    clientFound = true;
                    break;
                } else {
                    tempQueue.push(potential);
                }
            }

            // Возвращаем неподходящих клиентов обратно в очередь
            while (!tempQueue.empty()) {
                bankQueue.push(tempQueue.front());
                tempQueue.pop();
            }
        }

        if (!clientFound) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                // Обработка найденного клиента
                {
                    lock_guard<mutex> visualLock(visualsMutex);
                    client.shape.setPosition(620 + (workerId % 3) * 100, 200 + (workerId / 3) * 100);
                    visuals[workerId].setFillColor(sf::Color::Blue); // Работает
                }

                // Лог начала обслуживания
                {
                    lock_guard<mutex> logLock(logsMutex);
                    logs.push_back("Worker " + to_string(workerId+1) + " started processing client " +
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
                    visuals[workerId+1].setFillColor(sf::Color::Yellow); // Свободен
                }

                // Обновление статистики
                {
                    lock_guard<mutex> statsLock(stats.statsMutex);
                    stats.totalClients++;
                    stats.totalServiceTime += elapsed.count();

                    if (client.operation == "Deposit") {
                        stats.deposits++;
                        stats.depositProfit += client.amount * 0.01; // 1% комиссия за депозит
                    }
                    else if (client.operation == "Withdraw") {
                        stats.withdrawals++;
                        stats.withdrawalProfit += client.amount * 0.02; // 2% комиссия за снятие
                    }
                    else if (client.operation == "Loan") {
                        stats.loans++;
                        stats.loanProfit += client.amount * 0.05; // 5% комиссия за кредит
                    }
                    else if (client.operation == "Inquiry") {
                        stats.inquiries++;
                        stats.inquiryProfit += 5.0; // Фиксированная плата за консультацию
                    }
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
    }


    void poissonClientGenerator(int& clientId, mutex& logsMutex, vector<string>& logs,
                              deque<double>& timeIntervals, mutex& intervalsMutex) {

        std::default_random_engine generator(static_cast<unsigned>(time(0)));
        std::exponential_distribution<double> exp_dist(lambdaRate.load());
        double interval = exp_dist(generator);
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
        auto lastTime = chrono::steady_clock::now();  // замер времени

        while (!resetRequested.load() && bankOpen) {
            if (resetRequested.load()) break;
            exponential_distribution<double> exp_dist(lambdaRate.load());
            double interval = exp_dist(generator); //  интервал
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
        addText("Deposits: " + to_string(stats.deposits) +
               " ($" + to_string(stats.depositProfit).substr(0, 5) + ")");
        addText("Withdrawals: " + to_string(stats.withdrawals) +
               " ($" + to_string(stats.withdrawalProfit).substr(0, 5) + ")");
        addText("Loans: " + to_string(stats.loans) +
               " ($" + to_string(stats.loanProfit).substr(0, 5) + ")");
        addText("Inquiries: " + to_string(stats.inquiries) +
               " ($" + to_string(stats.inquiryProfit).substr(0, 5) + ")");

        double totalProfit = stats.depositProfit + stats.withdrawalProfit +
                            stats.loanProfit + stats.inquiryProfit;
        addText("----------------------");
        addText("Total profit: $" + to_string(totalProfit).substr(0, 6));

        double avgService = stats.totalClients > 0 ? stats.totalServiceTime / stats.totalClients : 0;
        double avgWait = stats.totalClients > 0 ? stats.totalWaitTime / stats.totalClients : 0;

        addText("Avg service time: " + to_string(avgService).substr(0, 4) + "s");
        addText("Avg wait time: " + to_string(avgWait).substr(0, 4) + "s");

        statsWindow.display();
    }
    int showWorkerSelectionWindow() {
    sf::RenderWindow selectionWindow(sf::VideoMode(600, 300), "Select Number of Cashiers");
    sf::Font font;
    if (!font.loadFromFile("C:/Users/1/CLionProjects/HELLOSFML/fonts/arial/arial.ttf")) {
        return 3; // значение по умолчанию
    }

    int selected = 3;
    bool confirmed = false;

    // Создаем кнопки выбора (1-10)
    vector<sf::RectangleShape> buttons;
    vector<sf::Text> buttonTexts;

    // Первая строка кнопок (1-5)
    for (int i = 0; i < 5; i++) {
        sf::RectangleShape button(sf::Vector2f(50, 40));
        button.setPosition(50 + i * 110, 100);
        button.setFillColor(i == 2 ? sf::Color::Green : sf::Color::White);
        buttons.push_back(button);

        sf::Text text(to_string(i+1), font, 20);
        text.setPosition(65 + i * 110, 105);
        text.setFillColor(sf::Color::Black);
        buttonTexts.push_back(text);
    }

    // Вторая строка кнопок (6-10)
    for (int i = 5; i < 10; i++) {
        sf::RectangleShape button(sf::Vector2f(50, 40));
        button.setPosition(50 + (i-5) * 110, 160);
        button.setFillColor(sf::Color::White);
        buttons.push_back(button);

        sf::Text text(to_string(i+1), font, 20);
        text.setPosition(65 + (i-5) * 110, 165);
        text.setFillColor(sf::Color::Black);
        buttonTexts.push_back(text);
    }

    // Кнопка OK
    sf::RectangleShape okButton(sf::Vector2f(100, 50));
    okButton.setPosition(250, 220);
    okButton.setFillColor(sf::Color(0, 250, 154));
    sf::Text okText("OK", font, 24);
    okText.setPosition(280, 230);
    okText.setFillColor(sf::Color::White);

    sf::Text title("Select number of cashiers (1-10):", font, 24);
    title.setPosition(50, 30);
    title.setFillColor(sf::Color::White);

    while (selectionWindow.isOpen()) {
        sf::Event event;
        while (selectionWindow.pollEvent(event)) {
            if (event.type == sf::Event::Closed) {
                selectionWindow.close();
                return 3;
            }

            if (event.type == sf::Event::MouseButtonPressed) {
                if (event.mouseButton.button == sf::Mouse::Left) {
                    sf::Vector2f mousePos = selectionWindow.mapPixelToCoords(
                        sf::Vector2i(event.mouseButton.x, event.mouseButton.y));

                    // Проверяем нажатие на кнопки с цифрами
                    for (int i = 0; i < buttons.size(); i++) {
                        if (buttons[i].getGlobalBounds().contains(mousePos)) {
                            selected = i + 1;
                            // Обновляем цвета всех кнопок
                            for (int j = 0; j < buttons.size(); j++) {
                                buttons[j].setFillColor(j == i ? sf::Color::Green : sf::Color::White);
                            }
                            break;
                        }
                    }

                    // Проверяем нажатие на кнопку OK
                    if (okButton.getGlobalBounds().contains(mousePos)) {
                        selectionWindow.close();
                    }
                }
            }
        }

        selectionWindow.clear(sf::Color(50, 50, 70));
        selectionWindow.draw(title);
        for (auto& btn : buttons) selectionWindow.draw(btn);
        for (auto& text : buttonTexts) selectionWindow.draw(text);
        selectionWindow.draw(okButton);
        selectionWindow.draw(okText);
        selectionWindow.display();
    }

    return selected;
}
    int main() {
        srand(time(0));
        int clientId = 1;
        // Показываем окно выбора количества касс
        numWorkers = showWorkerSelectionWindow();

        // Инициализируем специализацию касс
        workerSpecialization.clear();
        for (int i = 0; i < numWorkers; ++i) {
            if (i == 0) workerSpecialization.push_back("Deposit");
            else if (i == 1) workerSpecialization.push_back("Withdraw");
            else if (i == 2) workerSpecialization.push_back("Loan");
            else if (i == 3) workerSpecialization.push_back("Inquiry");
            else workerSpecialization.push_back("Any"); // Остальные кассы универсальные
        }
        vector<sf::RectangleShape> workersVisuals;
        vector<thread> workers;
        vector<string> logs;
        mutex logsMutex;
        bool simulationStarted = false;

        thread poissonGenerator;
        sf::RenderWindow window(sf::VideoMode(1300, 700), "Bank Simulator");
        sf::Font font;
        if (!font.loadFromFile("C:/Users/1/CLionProjects/HELLOSFML/fonts/arial/arial.ttf")) {
            cerr << "Error loading font!" << endl;
            return -1;
        }
        const size_t MAX_LOG_LINES = 100;  // Максимальное количество хранимых логов
        const float LOG_START_Y = 15.f;    // Начальная позиция Y для первого лога
        const float LINE_HEIGHT = 20.f;    // Высота строки лога

        workersVisuals.clear();
        int currentWorkers = numWorkers.load();
        for (int i = 0; i < currentWorkers; ++i) {
            sf::RectangleShape worker(sf::Vector2f(40, 40));
            worker.setFillColor(sf::Color::Yellow);
            // Позиционируем кассы - максимум 5 в ряд
            int cols = currentWorkers <= 5 ? currentWorkers : 5;
            worker.setPosition(750 + (i % cols) * 100, 100 + (i / cols) * 120);
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
        sf::View logView(sf::FloatRect(0, 0, 800, 700));
        logView.setViewport(sf::FloatRect(0.f, 0.f, 0.615f, 1.f));  // 60% ширины

        // Правый — для визуальной части
        sf::View visualView(sf::FloatRect(0, 0, 500, 700));
        visualView.setViewport(sf::FloatRect(0.615f, 0.f, 0.385f, 1.f));
        // Кнопки
        auto startButton = createButton(850, 550, 120, 40, sf::Color::Green);
        auto startText = createText(font, "Start", 880, 560, 18, sf::Color::Black);

        sf::RectangleShape stopButton(sf::Vector2f(120, 40));
        stopButton.setPosition(1000, 550);
        stopButton.setFillColor(sf::Color::Red);
        // Кнопки управления mu
        auto increaseMuButton = createButton(850, 500, 40, 30, sf::Color::White);
        sf::Text increaseMuText = createText(font, "+", 865, 500, 20, sf::Color::Black);
        sf::Text muText = createText(font, "mu = 0.5", 850, 470, 20, sf::Color::Cyan);
        auto decreaseMuButton = createButton(900, 500, 40, 30, sf::Color::White);
        sf::Text decreaseMuText = createText(font, "-", 915, 500, 20, sf::Color::Black);
        // Кнопки управления lambda
        sf::RectangleShape increaseLambdaButton = createButton(700, 500, 40, 30, sf::Color::White);
        sf::Text increaseText = createText(font, "+", 715, 500, 20, sf::Color::Black);
        sf::Text lambdaText = createText(font, "lambda = 1.0", 700, 470, 20, sf::Color::Magenta);
        sf::RectangleShape decreaseLambdaButton = createButton(750, 500, 40, 30, sf::Color::White);
        sf::Text decreaseText = createText(font, "-", 765, 500, 20, sf::Color::Black);

        sf::Text stopText("Stop", font, 18);
        stopText.setPosition(1030, 560);
        stopText.setFillColor(sf::Color::Black);
        sf::RectangleShape timeline(sf::Vector2f(200, 10));
        timeline.setPosition(550, 380);
        timeline.setFillColor(sf::Color::Blue);

        sf::RectangleShape resetButton(sf::Vector2f(120, 40));
        resetButton.setPosition(1000, 600);  // Под  Stop
        resetButton.setFillColor(sf::Color(100, 100, 255));
        sf::Text resetText("Reset", font, 18);
        resetText.setPosition(1030, 610);
        resetText.setFillColor(sf::Color::White);

        // Индикатор очереди - фон
        sf::RectangleShape queueIndicatorBg(sf::Vector2f(120, 30));
        queueIndicatorBg.setPosition(850, 600);  // Под кнопками
        queueIndicatorBg.setFillColor(sf::Color(50, 50, 50));
        queueIndicatorBg.setOutlineThickness(1);
        queueIndicatorBg.setOutlineColor(sf::Color::White);

        // Текст индикатора очереди
        sf::Text queueSizeText("Queue: 0", font, 18);
        queueSizeText.setPosition(860, 603);  // Смещение внутри фона
        queueSizeText.setFillColor(sf::Color::White);
        sf::Text statsText("", font, 16);
        statsText.setPosition(700, 400);
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
        sf::RectangleShape separator(sf::Vector2f(2, 700));
        separator.setPosition(800, 0); // граница между логами и визуализацией
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
                                bankOpen = true;
                                resetRequested = false;
                                // Убедитесь, что предыдущие потоки завершены
                                if (poissonGenerator.joinable()) {
                                    poissonGenerator.join();
                                }
                                for (auto& worker : workers) {
                                    if (worker.joinable()) {
                                        worker.join();
                                    }
                                }
                                workers.clear();

                                poissonGenerator = thread(poissonClientGenerator, ref(clientId), ref(logsMutex),
                                                          ref(logs), ref(timeIntervals), ref(intervalsMutex));

                                for (int i = 0; i < numWorkers; i++) {
                                    workers.emplace_back(processClient, i, ref(logs), ref(logsMutex),
                                                         ref(workersVisuals), ref(visualsMutex));
                                }
                                simulationStarted = true;

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
                            // 1. Устанавливаем флаги для остановки
                            resetRequested.store(true);
                            bankOpen = false;
                            simulationRunning.store(false);
                            cv.notify_all();  // Будим все потоки

                            // 2. Ожидаем завершения генератора клиентов
                            if (simulationStarted && poissonGenerator.joinable()) {
                                poissonGenerator.join();
                            }

                            // 3. Ожидаем завершения всех воркеров
                            for (auto& worker : workers) {
                                if (worker.joinable()) {
                                    worker.join();
                                }
                            }
                            workers.clear();  // Очищаем вектор потоков

                            // 4. Сбрасываем данные
                            {
                                lock_guard<mutex> lock(queueMutex);
                                bankQueue = queue<Client>();  // Очищаем очередь
                                processedClients.clear();
                            }

                            // 5. Сбрасываем статистику
                            stats.reset();
                            workerSpecialization.clear();
                            for (int i = 0; i < numWorkers; ++i) {
                                if (i == 0) workerSpecialization.push_back("Deposit");
                                else if (i == 1) workerSpecialization.push_back("Withdraw");
                                else if (i == 2) workerSpecialization.push_back("Loan");
                                else if (i == 3) workerSpecialization.push_back("Inquiry");
                                else workerSpecialization.push_back("Any");
                            }
                            clientId = 1;  // Сбрасываем ID клиентов

                            // 6. Очищаем логи
                            {
                                lock_guard<mutex> lock(logsMutex);
                                logs.clear();
                                logs.push_back("Simulation reset. Ready to start again.");
                            }

                            // 7. Визуальный сброс
                            {
                                lock_guard<mutex> lock(visualsMutex);
                                for (auto& worker : workersVisuals) {
                                    worker.setFillColor(sf::Color::Yellow);
                                }
                            }

                            // 8. Сбрасываем флаги
                            resetRequested.store(false);
                            bankOpen = true;
                            simulationRunning.store(true);
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
                // Отрисовка работников и их специализаций
                for (int i = 0; i < workersVisuals.size(); ++i) {
                    window.draw(workersVisuals[i]);

                    sf::Text label("Cashier " + to_string(i+1) + "\n(" + workerSpecialization[i] + ")", font, 14);
                    label.setFillColor(sf::Color(200, 200, 255));
                    label.setPosition(workersVisuals[i].getPosition().x, workersVisuals[i].getPosition().y - 40);
                    window.draw(label);
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

        return 0;
    }