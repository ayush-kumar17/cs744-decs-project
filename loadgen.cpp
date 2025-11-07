#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>
#include <random>
#include <curl/curl.h>

using namespace std;

struct Stats {
    atomic<long long> success{0};
    atomic<long long> fail{0};
    atomic<long long> total_ns{0};
};

size_t write_callback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    return size * nmemb; // discard body
}

void worker(string base, string mode, int duration_s, int popular_k, Stats &st, int seed_offset) {
    CURL* curl = curl_easy_init();
    if (!curl) return;

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    mt19937_64 rng(random_device{}() + seed_offset);
    uniform_int_distribution<int> dist(0, 1000000);
    uniform_int_distribution<int> popdist(0, max(1, popular_k)-1);

    auto end = chrono::steady_clock::now() + chrono::seconds(duration_s);

    while (chrono::steady_clock::now() < end) {

        string key, url, body;
        bool is_put = false;

        if (mode == "put-all") {
            key = "k_" + to_string(dist(rng));
            is_put = true;
        }
        else if (mode == "get-all") {
            key = "k_" + to_string(dist(rng));
        }
        else if (mode == "get-popular") {
            key = "popular_" + to_string(popdist(rng));
        }
        else { // mixed reads + writes
            key = "k_" + to_string(dist(rng));
            if (dist(rng) % 3 == 0) is_put = true;
        }

        url = base + "/kv/" + key;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

        if (is_put) {
            body = "v_" + to_string(dist(rng));
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, NULL);
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, NULL);
        }

        auto t0 = chrono::steady_clock::now();
        auto res = curl_easy_perform(curl);
        auto t1 = chrono::steady_clock::now();

        long long ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();
        st.total_ns += ns;

        if (res == CURLE_OK) st.success++;
        else st.fail++;

        if (st.success % 10000 == 0)
        cout << "Progress: " << st.success << " requests done" << endl;

    }

    curl_easy_cleanup(curl);
}

int main(int argc, char** argv) {
    if (argc < 6) {
        cout << "Usage: ./loadgen <base-url> <mode> <threads> <duration-s> <popular-k>\n";
        cout << "Modes: put-all, get-all, get-popular, mixed\n";
        return 1;
    }

    string base = argv[1];
    string mode = argv[2];
    int threads = stoi(argv[3]);
    int duration = stoi(argv[4]);
    int popular_k = stoi(argv[5]);

    curl_global_init(CURL_GLOBAL_DEFAULT);

    Stats st;
    vector<thread> pool;

    for (int i = 0; i < threads; i++)
        pool.emplace_back(worker, base, mode, duration, popular_k, ref(st), i);

    for (auto &t : pool) t.join();

    curl_global_cleanup();

    long long s = st.success.load();
    long long f = st.fail.load();
    double avg_ms = s ? (st.total_ns.load() / 1e6) / s : 0;

    cout << "Success=" << s << " Fail=" << f << " AvgLatency(ms)=" << avg_ms << "\n";
    return 0;
}
