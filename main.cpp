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

using namespace std;

mutex queueMutex;
condition_variable cv;
bool bankOpen = true;

// Клиент банка
struct Client {
    int id;
    string operation;
    int amount;

    Client(int id, string operation, int amount)
        : id(id), operation(operation), amount(amount) {}
};

queue<Client> bankQueue;
vector<Client> processedClients;

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
void processClient(int workerId, vector<string>& logs, mutex& logsMutex) {
    while (true) {
        unique_lock<mutex> lock(queueMutex);
        cv.wait(lock, [] { return !bankQueue.empty() || !bankOpen; });

        if (!bankOpen && bankQueue.empty()) {
            break;
        }

        Client client = bankQueue.front();
        bankQueue.pop();
        lock.unlock();

        string logEntry = "Worker " + to_string(workerId) + " processing client " + to_string(client.id) +
                          " - " + client.operation + " ($" + to_string(client.amount) + ")";

        {
            lock_guard<mutex> logLock(logsMutex);
            logs.push_back(logEntry);
        }

        this_thread::sleep_for(chrono::seconds(2)); // Симуляция обработки

        lock.lock();
        processedClients.push_back(client);
        lock.unlock();
    }
}

int main() {
    srand(time(0));
    int clientId = 1;
    const int numWorkers = 3;
    vector<thread> workers;
    vector<string> logs;
    mutex logsMutex;

    // Запуск потоков сотрудников
    for (int i = 0; i < numWorkers; i++) {
        workers.emplace_back(processClient, i + 1, ref(logs), ref(logsMutex));
    }

    sf::RenderWindow window(sf::VideoMode(600, 400), "Bank Simulator");
    sf::Font font;
    if (!font.loadFromFile("C:/Users/1/CLionProjects/HELLOSFML/fonts/Roboto/Roboto-Italic-VariableFont_wdth,wght.ttf")) {
        cerr << "Error loading font!" << endl;
        return -1;
    }

    // Текст для логов
    sf::Text logText("Bank Simulation Log:\n", font, 16);
    logText.setPosition(10, 10);
    logText.setFillColor(sf::Color::White);

    // Кнопки
    sf::RectangleShape startButton(sf::Vector2f(120, 40));
    startButton.setPosition(10, 350);
    startButton.setFillColor(sf::Color::Green);

    sf::Text startText("Start", font, 18);
    startText.setPosition(35, 360);
    startText.setFillColor(sf::Color::Black);

    sf::RectangleShape stopButton(sf::Vector2f(120, 40));
    stopButton.setPosition(150, 350);
    stopButton.setFillColor(sf::Color::Red);

    sf::Text stopText("Stop", font, 18);
    stopText.setPosition(185, 360);
    stopText.setFillColor(sf::Color::Black);

    // Переменные для прокрутки
    float scrollOffset = 0.0f;
    const float scrollSpeed = 20.0f; // Скорость прокрутки
    const float maxScrollOffset = 1000.0f; // Максимальное смещение

    // Создаем вид (View) для текстовой области
    sf::View textView(sf::FloatRect(10, 10, 580, 330)); // Область текста
    textView.setViewport(sf::FloatRect(0.0f, 0.0f, 1.0f, 0.8f)); // 80% окна для текста

    while (window.isOpen()) {
        sf::Event event;
        while (window.pollEvent(event)) {
            if (event.type == sf::Event::Closed)
                window.close();

            if (event.type == sf::Event::MouseButtonPressed) {
                if (event.mouseButton.button == sf::Mouse::Left) {
                    sf::Vector2i mousePos = sf::Mouse::getPosition(window);

                    if (startButton.getGlobalBounds().contains(mousePos.x, mousePos.y)) {
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

        window.clear(sf::Color::Black);

        // Устанавливаем вид для текста
        window.setView(textView);

        // Отрисовка текста
        string logString = "Bank Simulation Log:\n";
        {
            lock_guard<mutex> logLock(logsMutex);
            for (const string& log : logs) {
                logString += log + "\n";
            }
        }
        logText.setString(logString);
        window.draw(logText);

        // Возвращаем вид по умолчанию для отрисовки кнопок
        window.setView(window.getDefaultView());

        // Отрисовка кнопок
        window.draw(startButton);
        window.draw(startText);
        window.draw(stopButton);
        window.draw(stopText);

        window.display();
    }

    for (auto& worker : workers) {
        worker.join();
    }

    return 0;
}