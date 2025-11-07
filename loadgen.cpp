#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <curl/curl.h>

using namespace std;

int main(int argc, char* argv[]) {
    if (argc != 5) {
        cout << "Usage: ./loadgen <url> <key> <threads> <requests>\n";
        return 1;
    }

    string url = argv[1];
    string key = argv[2];
    int threads = stoi(argv[3]);
    int total = stoi(argv[4]);

    atomic<int> ok{0}, fail{0};

    auto worker = [&]() {
        CURL* curl = curl_easy_init();
        string full = url + key;
        for (int i = 0; i < total; i++) {
            curl_easy_setopt(curl, CURLOPT_URL, full.c_str());
            if (curl_easy_perform(curl) == CURLE_OK) ok++;
            else fail++;
        }
        curl_easy_cleanup(curl);
    };

    vector<thread> v;
    auto start = chrono::high_resolution_clock::now();
    for (int i = 0; i < threads; i++) v.emplace_back(worker);
    for (auto &t : v) t.join();
    auto end = chrono::high_resolution_clock::now();

    double ms = chrono::duration<double, milli>(end - start).count();
    cout << "Success=" << ok << " Fail=" << fail << " Time(ms)=" << ms << endl;
}
