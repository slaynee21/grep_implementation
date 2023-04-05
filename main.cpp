#include <iostream>
#include <fstream>
#include <vector>
#include <regex>
#include <string>
#include <filesystem>
#include <chrono>
#include <thread>
#include <mutex>
#include <map>
#include <condition_variable>
#include <future>
#include <queue>
#include <functional>

using namespace std;

struct Result {
    string file_path;
    int line_number{};
    string line_content;
};

class ThreadPool {
public:
    explicit ThreadPool(size_t threads) : stop(false), unfinished_tasks(0) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    function<void()> task;
                    {
                        unique_lock<mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty()) {
                            return;
                        }
                        task = move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                    {
                        unique_lock<mutex> lock(queue_mutex);
                        --unfinished_tasks;
                    }
                    condition.notify_one();
                }
            });
        }
    }

    void wait() {
        {
            unique_lock<mutex> lock(queue_mutex);
            condition.wait(lock, [this] { return tasks.empty() && unfinished_tasks == 0; });
        }
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &worker : workers) {
            worker.join();
        }
    }


    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> future<typename result_of<F(Args...)>::type> {
        using return_type = typename result_of<F(Args...)>::type;
        auto task = make_shared<packaged_task<return_type()>>(bind(forward<F>(f), forward<Args>(args)...));
        future<return_type> res = task->get_future();
        {
            unique_lock<mutex> lock(queue_mutex);
            if (stop) {
                throw runtime_error("ThreadPool is stopped");
            }
            tasks.emplace([task]() { (*task)(); });
            ++unfinished_tasks;
        }
        condition.notify_one();
        return res;
    }


    ~ThreadPool() {
        {
            unique_lock<mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (thread &worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }


private:
    vector<thread> workers;
    queue<function<void()>> tasks;
    mutex queue_mutex;
    condition_variable condition;
    bool stop;
    size_t unfinished_tasks;

};

mutex results_mutex;
mutex log_mutex;
map<thread::id, vector<string>> thread_logs;

void search_pattern_in_file(const string& pattern, const string& file_path, vector<Result>& results) {
    ifstream file(file_path);
    if (!file) {
        return;
    }
    string line;
    int line_number = 0;
    regex re(pattern);
    while (getline(file, line)) {
        line_number++;
        if (regex_search(line, re)) {
            Result res;
            res.file_path = file_path;
            res.line_number = line_number;
            res.line_content = line;
            lock_guard<mutex> lock(results_mutex);
            results.push_back(res);
        }
    }
    lock_guard<mutex> lock_log(log_mutex);
    thread_logs[this_thread::get_id()].push_back(filesystem::path(file_path).filename().string());
}

void search_files(const string& pattern, const filesystem::path& root, ThreadPool& pool, vector<Result>& results, vector<future<void>>& futures){
    for(const auto& entry : filesystem::directory_iterator(root)) {
        if (entry.is_regular_file()) {
            futures.push_back(pool.enqueue(search_pattern_in_file, pattern, entry.path().string(), ref(results)));
        } else if (entry.is_directory()) {
            search_files(pattern, entry.path(), pool, results, futures);
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <pattern> [-d/--dir <start_directory>] [-l/--log_file <log_file_name>] [-r/--result_file <result_file_name>] [-t/--threads <number_of_threads>]" << endl;
        return 1;
    }

    string pattern = argv[1];
    string start_directory = ".";
    string log_file_name = "specific_grep.log";
    string result_file_name = "specific_grep.txt";
    int num_threads = 4;

    for (int i = 2; i < argc; i++) {
        string arg = argv[i];
        if (arg == "-d" || arg == "--dir") {
            if (i + 1 < argc) {
                start_directory = argv[++i];
            }
        } else if (arg == "-l" || arg == "--log_file") {
            if (i + 1 < argc) {
                log_file_name = argv[++i];
            }
        } else if (arg == "-r" || arg == "--result_file") {
            if (i + 1 < argc) {
                result_file_name = argv[++i];
            }
        } else if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                num_threads = stoi(argv[++i]);
            }
        }
    }

    auto start_time = chrono::steady_clock::now();
    vector<Result> results;
    ThreadPool pool(num_threads);

    vector<future<void>> futures;
    search_files(pattern, start_directory, pool, results, futures);
    pool.wait();




    ofstream result_file(result_file_name);
    sort(results.begin(), results.end(), [](const Result& a, const Result& b) {
        return a.file_path < b.file_path;
    });

    for (const auto& res : results) {
        result_file << res.file_path << ":" << res.line_number << ": " << res.line_content << endl;
    }
    result_file.flush();


    ofstream log_file(log_file_name);
    for (const auto& [thread_id, file_names] : thread_logs) {
        log_file << thread_id << ": ";
        for (const auto& file_name : file_names) {
            log_file << file_name << ",";
        }
        log_file << endl;
    }
    log_file.flush();




    auto end_time = chrono::steady_clock::now();
    auto elapsed_time = chrono::duration_cast<chrono::milliseconds>(end_time - start_time).count();

    cout << "Searched files: " << results.size() << endl;
    cout << "Files with pattern: " << thread_logs.size() << endl;
    cout << "Patterns number: " << results.size() << endl;
    cout << "Result file: " << result_file_name << endl;
    cout << "Log file: " << log_file_name << endl;
    cout << "Used threads: " << num_threads << endl;
    cout << "Elapsed time: " << elapsed_time << "[ms]" << endl;

    return 0;
}