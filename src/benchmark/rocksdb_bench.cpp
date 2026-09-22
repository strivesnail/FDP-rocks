#include <gflags/gflags.h>
#include <condition_variable>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>  // for std::shuffle
#include <vector>     // for std::vector
#include <unistd.h>  // for sysconf
#include <fstream>    // for file I/O
#include <sstream>    // for stringstream
#include <sys/stat.h> // for mkdir
#include <cmath>      // for std::pow
#include <sys/time.h> // for gettimeofday
#include <sys/statvfs.h>
#include <cstdio>
#include <cinttypes>  // for PRIu64

#include "rocksdb/convenience.h"
#include "rocksdb/db.h"
#include "rocksdb/env.h"
#include "rocksdb/sst_file_manager.h"
#include "rocksdb/statistics.h"
#include "slice.h"
#include "util.h"
#include "indexkey.h"

using GFLAGS_NAMESPACE::ParseCommandLineFlags;
using namespace util;
using Key = IndexKey;

namespace {
// ROCKSDB_COMPACTION_PRI: roundrobin/bcs/min/olseq/osseq (unset -> kMinOverlappingRatio).
rocksdb::CompactionPri CompactionPriFromEnv() {
  const char* e = std::getenv("ROCKSDB_COMPACTION_PRI");
  if (e == nullptr || e[0] == '\0') {
    return rocksdb::kMinOverlappingRatio;
  }
  std::string v(e);
  for (char& c : v) {
    c = static_cast<char>(
        ::toupper(static_cast<unsigned char>(c)));
  }
  if (v == "KROUNDROBIN" || v == "ROUNDROBIN" || v == "RR" || v == "4") {
    return rocksdb::kRoundRobin;
  }
  if (v == "KBYCOMPENSATEDSIZE" || v == "BYCOMPENSATEDSIZE" || v == "BCS" ||
      v == "0") {
    return rocksdb::kByCompensatedSize;
  }
  if (v == "KMINOVERLAPPINGRATIO" || v == "MINOVERLAPPING" || v == "MIN" ||
      v == "MOR" || v == "3") {
    return rocksdb::kMinOverlappingRatio;
  }
  if (v == "KOLDESTLARGESTSEQFIRST" || v == "OLSEQ" || v == "1") {
    return rocksdb::kOldestLargestSeqFirst;
  }
  if (v == "KOLDESTSMALLESTSEQFIRST" || v == "OSSEQ" || v == "2") {
    return rocksdb::kOldestSmallestSeqFirst;
  }
  std::cerr << "[rocksdb_bench][WARN] Unknown ROCKSDB_COMPACTION_PRI=" << e
            << "; using kMinOverlappingRatio.\n";
  return rocksdb::kMinOverlappingRatio;
}

const char* CompactionPriLabel(rocksdb::CompactionPri cp) {
  switch (cp) {
    case rocksdb::kByCompensatedSize:
      return "kByCompensatedSize";
    case rocksdb::kOldestLargestSeqFirst:
      return "kOldestLargestSeqFirst";
    case rocksdb::kOldestSmallestSeqFirst:
      return "kOldestSmallestSeqFirst";
    case rocksdb::kMinOverlappingRatio:
      return "kMinOverlappingRatio";
    case rocksdb::kRoundRobin:
      return "kRoundRobin";
    default:
      return "Unknown";
  }
}
}  // namespace

DEFINE_uint32 (worker_threads, 1, "number of worker threads");
DEFINE_uint64 (report_interval, 1, "Report interval in seconds");
DEFINE_uint64 (stats_interval, 100000000, "Report interval in ops");
DEFINE_uint64 (value_size, 800, "The value size");
DEFINE_uint64 (num, 15000000, "Key space size (range: [1, num]), also the number of writes");
DEFINE_string (db1_path, "", "RocksDB database path");
DEFINE_uint32 (batch, 10000, "rocksdb batch size");
DEFINE_uint32 (add_delay, 0, "Adds a delay in milliseconds to each batch write");
DEFINE_string (key_order_dir, "", "Directory to store/load key order files");
DEFINE_string (key_distribution, "uniform", "Key distribution: 'uniform', 'zipfian', or 'hotspot_zipfian'");
DEFINE_string (key_sort, "random", "Key sort order for uniform distribution: 'random', 'ascending', 'descending', or 'random_seed<N>' (e.g., 'random_seed1', 'random_seed2')");
DEFINE_string (key_file_path, "", "Direct path to key order file (if specified, this file will be used instead of auto-generated path)");

namespace {

static void TransformKey (uint64_t rid, IndexKey& index_key) {
    index_key.setKeyLen (sizeof (rid));
    reinterpret_cast<uint64_t*> (&index_key[0])[0] = __builtin_bswap64 (rid);
}

static std::string GetTimestamp() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    char buf[64];
    snprintf(buf, sizeof(buf), "%ld.%06ld", tv.tv_sec, tv.tv_usec);
    return std::string(buf);
}

static std::string GetDiskUsage() {
    struct statvfs info;
    if (FLAGS_db1_path.empty() || statvfs(FLAGS_db1_path.c_str(), &info) != 0) {
        return "N/A";
    }
    const double scale = 1024.0 * 1024.0 * 1024.0;
    const double total = info.f_blocks * info.f_frsize / scale;
    const double available = info.f_bavail * info.f_frsize / scale;
    char buf[128];
    snprintf(buf, sizeof(buf), "%.1fGiB/%.1fGiB", total - available, total);
    return std::string(buf);
}

class Stats {
public:
    int tid_;
    double start_;
    double finish_;
    double seconds_;
    double next_report_time_;
    uint64_t done_;
    uint64_t next_report_;
    uint64_t total_ops_;
    uint64_t last_rocksdb_stats_time_;
    uint64_t next_10k_boundary_;
    double prev_10k_time_;
    double curr_10k_time_;

    explicit Stats (int id) : tid_ (id), total_ops_(0), next_10k_boundary_(10000), prev_10k_time_(0), curr_10k_time_(0) { Start (); }

    void SetTotalOps (uint64_t total) { total_ops_ = total; }

    void Start () {
        start_ = NowMicros ();
        next_report_time_ = start_ + FLAGS_report_interval * 1000000;
        next_report_ = 100;
        last_rocksdb_stats_time_ = start_;
        done_ = 0;
        seconds_ = 0;
        finish_ = start_;
        next_10k_boundary_ = 10000;
        prev_10k_time_ = 0;
        curr_10k_time_ = 0;
    }

    void Merge (const Stats& other) {
        done_ += other.done_;
        seconds_ += other.seconds_;
        if (other.start_ < start_) start_ = other.start_;
        if (other.finish_ > finish_) finish_ = other.finish_;
    }

    void Stop () {
        finish_ = NowMicros ();
        seconds_ = (finish_ - start_) * 1e-6;
    }

    void PrintProgressBar () {
        if (total_ops_ == 0) {
                fprintf (stderr, "Thread %d: %llu ops completed\r", tid_, (unsigned long long)done_);
            return;
        }

        double percentage = (double)done_ / total_ops_ * 100.0;
        if (percentage > 100.0) percentage = 100.0;

        const int bar_width = 50;
        int filled = (int)(percentage / 100.0 * bar_width);
        if (filled > bar_width) filled = bar_width;

        std::string bar;
        bar.reserve (bar_width + 10);
        bar += "[";
        for (int i = 0; i < bar_width; i++) {
            if (i < filled) {
                bar += "=";
            } else if (i == filled) {
                bar += ">";
            } else {
                bar += " ";
            }
        }
        bar += "]";

        uint64_t now = NowMicros ();
        double elapsed = (now - start_) / 1000000.0;
        double ops_per_sec = 0;
        if (next_10k_boundary_ >= 20000 && prev_10k_time_ > 0 && (curr_10k_time_ - prev_10k_time_) > 0) {
            ops_per_sec = 10000.0 / ((curr_10k_time_ - prev_10k_time_) / 1000000.0);
        } else if (elapsed > 0) {
            ops_per_sec = done_ / elapsed;
        }
        double remaining_ops = (total_ops_ > done_) ? (total_ops_ - done_) : 0;
        double eta_seconds = (ops_per_sec > 0) ? remaining_ops / ops_per_sec : 0;

        int eta_hours = (int)(eta_seconds / 3600);
        int eta_mins = (int)((eta_seconds - eta_hours * 3600) / 60);
        int eta_secs = (int)(eta_seconds - eta_hours * 3600 - eta_mins * 60);

            fprintf (stderr, "Thread %d: %s %.1f%% (%llu/%llu) | %.1f ops/s | ETA: %02d:%02d:%02d\r",
                     tid_, bar.c_str (), percentage, (unsigned long long)done_, (unsigned long long)total_ops_,
                     ops_per_sec, eta_hours, eta_mins, eta_secs);
        fflush (stderr);
    }

    inline bool FinishedBatchOp (size_t batch) {
        done_ += batch;
        while (done_ >= next_10k_boundary_) {
            prev_10k_time_ = curr_10k_time_;
            curr_10k_time_ = static_cast<double>(NowMicros());
            next_10k_boundary_ += 10000;
        }
        if ((done_ >= next_report_)) {
            if (next_report_ < 1000)
                next_report_ += 100;
            else if (next_report_ < 5000)
                next_report_ += 500;
            else if (next_report_ < 10000)
                next_report_ += 1000;
            else if (next_report_ < 50000)
                next_report_ += 5000;
            else if (next_report_ < 100000)
                next_report_ += 10000;
            else if (next_report_ < 500000)
                next_report_ += 50000;
            else
                next_report_ += 100000;

            PrintProgressBar ();

            if (FLAGS_report_interval == 0 && (done_ % FLAGS_stats_interval) == 0) {
                return 0;
            }
            fflush (stderr);
            fflush (stdout);
        }

        if (FLAGS_report_interval != 0 && NowMicros () > next_report_time_) {
            next_report_time_ += FLAGS_report_interval * 1000000;
            PrintProgressBar ();
            return 1;
        }
        return 0;
    }

    inline bool ShouldPrintRocksDBStats () {
        const uint64_t stats_interval = 30 * 1000000;
        uint64_t now = NowMicros ();
        if (now - last_rocksdb_stats_time_ >= stats_interval) {
            last_rocksdb_stats_time_ = now;
            return true;
        }
        return false;
    }

    void Report (const util::Slice& name) {
        if (done_ < 1) done_ = 1;
        double elapsed = (finish_ - start_) * 1e-6;
        double throughput = (double)done_ / elapsed;
        fprintf(stdout, "%-12s : %11.3f micros/op %lf Mops/s\n", name.ToString ().c_str (),
              elapsed * 1e6 / done_, throughput / 1024 / 1024);
        fflush (stdout);
        fflush (stderr);
    }
};

struct SharedState {
    std::mutex mu;
    std::condition_variable cv;
    int total;
    int num_initialized;
    int num_done;
    bool start;

    SharedState (int total) : total (total), num_initialized (0), num_done (0), start (false) {}
};

struct ThreadState {
    int tid;
    Stats stats;
    SharedState* shared;
    ThreadState (int index) : tid (index), stats (index) {}
};

class Benchmark {
public:
    uint64_t num_;
    int value_size_;
    rocksdb::DB* db1;
    rocksdb::Options options;
    rocksdb::WriteOptions woptions;
    std::shared_ptr<rocksdb::Statistics> statistics_;
    std::shared_ptr<rocksdb::SstFileManager> file_manager;
    std::vector<std::vector<uint64_t>> shuffled_keys_;

    Benchmark ()
        : num_ (FLAGS_num),
          value_size_ (FLAGS_value_size),
          db1(nullptr),
          options() {
        woptions.disableWAL = true;
        woptions.sync = false;

        options.max_background_compactions = 4;
        options.max_background_jobs = 5;
        options.level_compaction_dynamic_level_bytes = false;
        options.use_direct_io_for_flush_and_compaction = false;
        options.use_direct_reads = false;
        options.create_if_missing = true;
        statistics_ = rocksdb::CreateDBStatistics();
        options.statistics = statistics_;
        options.info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
        //
        //
        //
        //
        //
        //
        //
        //
        options.max_open_files = 400;
        options.compression = rocksdb::kNoCompression;

        options.write_buffer_size = 16 * 1024 * 1024;  // 16MB
        options.level0_file_num_compaction_trigger = 4;
        options.level0_slowdown_writes_trigger = 20;
        options.level0_stop_writes_trigger = 36;
        options.target_file_size_base = 16 * 1024 * 1024;  // 16MB
        options.target_file_size_multiplier = 1;
        options.max_bytes_for_level_base = (uint64_t)(200 * 1024 * 1024);  // 200 MB
        options.max_bytes_for_level_multiplier = 2;
        options.max_compaction_bytes = (uint64_t)(1024 * 1024 * 1024);  // 1GB

        file_manager.reset(rocksdb::NewSstFileManager(options.env));
        options.sst_file_manager = file_manager;
        options.compaction_style = rocksdb::kCompactionStyleLevel;
        options.compaction_pri = CompactionPriFromEnv();

        std::cerr << "=== RocksDB Configuration ===" << std::endl;
        std::cerr << "max_open_files: " << options.max_open_files << std::endl;
        std::cerr << "use_direct_reads: " << (options.use_direct_reads ? "true" : "false") << std::endl;
        std::cerr << "use_direct_io_for_flush_and_compaction: "
                  << (options.use_direct_io_for_flush_and_compaction ? "true" : "false") << std::endl;
        std::cerr << "max_background_compactions: " << options.max_background_compactions << std::endl;
        std::cerr << "compaction_pri: " << CompactionPriLabel(options.compaction_pri)
                  << std::endl;
        std::cerr << "System file descriptor limit: " << sysconf(_SC_OPEN_MAX) << std::endl;
        std::cerr << "=============================" << std::endl;

        const char* enable_phase2_env = getenv("ROCKSDB_ENABLE_PHASE2");
        const char* model_path_env = getenv("ROCKSDB_ML_MODELS_PATH");
        const char* predicted_compaction_env = getenv("ROCKSDB_ENABLE_PREDICTED_COMPACTION");
        const char* compaction_pri_ratio_env = getenv("ROCKSDB_COMPACTION_PRI_RATIO");
        const char* compaction_pri_env = getenv("ROCKSDB_COMPACTION_PRI");
        std::cerr << "[rocksdb_bench] Environment variables:" << std::endl;
        std::cerr << "  ROCKSDB_ENABLE_PHASE2=" << (enable_phase2_env ? enable_phase2_env : "(null)") << std::endl;
        std::cerr << "  ROCKSDB_COMPACTION_PRI=" << (compaction_pri_env ? compaction_pri_env : "(null)")
                  << std::endl;
        std::cerr << "  ROCKSDB_ML_MODELS_PATH=" << (model_path_env ? model_path_env : "(null)") << std::endl;
        std::cerr << "  ROCKSDB_ENABLE_PREDICTED_COMPACTION=" << (predicted_compaction_env ? predicted_compaction_env : "(null)") << std::endl;
        std::cerr << "  ROCKSDB_COMPACTION_PRI_RATIO=" << (compaction_pri_ratio_env ? compaction_pri_ratio_env : "(null)") << std::endl;
        std::cerr << "[rocksdb_bench] Opening database: " << FLAGS_db1_path << std::endl;
        std::cerr.flush();

        rocksdb::Status s = rocksdb::DB::Open (options, FLAGS_db1_path, &db1);
        std::cerr << "[rocksdb_bench] DB::Open result: " << (s.ok() ? "OK" : s.ToString()) << std::endl;
        std::cerr.flush();

        if (!s.ok ()) {
            std::cerr << "Cannot open database: " << s.ToString () << std::endl;
            std::cerr << "Note: If you see 'Too many open files', check:" << std::endl;
            std::cerr << "  1. System limit: ulimit -n" << std::endl;
            std::cerr << "  2. RocksDB max_open_files setting" << std::endl;
            std::cerr << "  3. Check if RocksDB is using more FDs than expected during compaction" << std::endl;
            exit (1);
        }
    }

    void Run () {
        if (num_ == 0) {
            std::cerr << "Error: --num must be greater than 0" << std::endl;
            exit (1);
        }

        PrintHeader ();
        PrintKeySpaceInfo ();

        PrepareShuffledKeys();

        RunBenchmark (FLAGS_worker_threads, "randomwrite", &Benchmark::DoSimpleRandomWrite);
        PrintRocksDBStats ();
    }

    std::string GetKeyOrderFilePath(int tid) {
        if (!FLAGS_key_file_path.empty()) {
            return FLAGS_key_file_path;
        }

        std::ostringstream oss;
        oss << FLAGS_key_order_dir << "/keys_num_" << num_
            << "_threads_" << FLAGS_worker_threads
            << "_dist_" << FLAGS_key_distribution;
        if (FLAGS_key_distribution == "uniform") {
            oss << "_sort_" << FLAGS_key_sort;
        }
        oss << "_tid_" << tid << ".bin";
        return oss.str();
    }

    bool LoadKeysFromFile(int tid, std::vector<uint64_t>& keys) {
        std::string filepath = GetKeyOrderFilePath(tid);
        std::ifstream file(filepath, std::ios::binary);

        if (!file.is_open()) {
            return false;
        }

        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);

        if (file_size % sizeof(uint64_t) != 0) {
            std::cerr << "Warning: Invalid key order file size: " << filepath << std::endl;
            file.close();
            return false;
        }

        size_t key_count = file_size / sizeof(uint64_t);
        keys.resize(key_count);

        file.read(reinterpret_cast<char*>(keys.data()), file_size);
        file.close();

        if (file.good()) {
            std::cerr << "Thread " << tid << ": Loaded " << key_count
                      << " keys from " << filepath << std::endl;
            return true;
        } else {
            std::cerr << "Warning: Failed to read key order file: " << filepath << std::endl;
            keys.clear();
            return false;
        }
    }

    bool SaveKeysToFile(int tid, const std::vector<uint64_t>& keys) {
        struct stat info;
        if (stat(FLAGS_key_order_dir.c_str(), &info) != 0) {
            if (mkdir(FLAGS_key_order_dir.c_str(), 0755) != 0) {
                std::cerr << "Error: Failed to create directory: " << FLAGS_key_order_dir << std::endl;
                return false;
            }
        } else if (!(info.st_mode & S_IFDIR)) {
            std::cerr << "Error: " << FLAGS_key_order_dir << " is not a directory" << std::endl;
            return false;
        }

        std::string filepath = GetKeyOrderFilePath(tid);
        std::ofstream file(filepath, std::ios::binary);

        if (!file.is_open()) {
            return false;
        }

        file.write(reinterpret_cast<const char*>(keys.data()),
                   keys.size() * sizeof(uint64_t));
        file.close();

        if (file.good()) {
            std::cerr << "Thread " << tid << ": Saved " << keys.size()
                      << " keys to " << filepath << std::endl;
            return true;
        } else {
            std::cerr << "Error: Failed to write key order file: " << filepath << std::endl;
            return false;
        }
    }

    void GenerateZipfianKeys(int tid, uint64_t key_count, std::vector<uint64_t>& keys) {
        uint64_t key_space_size = num_ / 4;
        if (key_space_size == 0) key_space_size = 1;

        double s = 1.0;

        double harmonic_sum = 0.0;
        for (uint64_t k = 1; k <= key_space_size; ++k) {
            harmonic_sum += 1.0 / std::pow(static_cast<double>(k), s);
        }

        std::vector<double> weights(key_space_size + 1);
        double weight_1 = 1.0 / (std::pow(1.0, s) * harmonic_sum);
        weights[0] = weight_1;

        for (uint64_t k = 1; k <= key_space_size; ++k) {
            weights[k] = 1.0 / (std::pow(static_cast<double>(k), s) * harmonic_sum);
        }

        double total_weight = 0.0;
        for (uint64_t k = 0; k <= key_space_size; ++k) {
            total_weight += weights[k];
        }
        for (uint64_t k = 0; k <= key_space_size; ++k) {
            weights[k] /= total_weight;
        }

        const uint64_t fixed_seed = 0x1234567890ABCDEFULL;
        std::mt19937 gen(fixed_seed + tid);
        std::discrete_distribution<uint64_t> dist(weights.begin(), weights.end());

        keys.clear();
        keys.reserve(key_count);
        for (uint64_t i = 0; i < key_count; ++i) {
            uint64_t key = dist(gen);
            keys.push_back(key);
        }

    }

    void GenerateHotspotZipfianKeys(uint32_t tid, uint64_t key_count, std::vector<uint64_t>& keys) {
        const uint64_t key_space_size = 60000000;
        const uint64_t num_hotspots = 60;
        const uint64_t hotspot_size = 1000000;
        const uint64_t keys_per_hotspot = 4000000;

        double s = 1.0;

        double harmonic_sum = 0.0;
        for (uint64_t k = 1; k <= hotspot_size; ++k) {
            harmonic_sum += 1.0 / std::pow(static_cast<double>(k), s);
        }

        std::vector<double> hotspot_weights(hotspot_size);
        for (uint64_t k = 0; k < hotspot_size; ++k) {
            hotspot_weights[k] = 1.0 / (std::pow(static_cast<double>(k + 1), s) * harmonic_sum);
        }

        double total_weight = 0.0;
        for (uint64_t k = 0; k < hotspot_size; ++k) {
            total_weight += hotspot_weights[k];
        }
        for (uint64_t k = 0; k < hotspot_size; ++k) {
            hotspot_weights[k] /= total_weight;
        }

        const uint64_t fixed_seed = 0x1234567890ABCDEFULL;
        std::mt19937 gen(fixed_seed + tid);
        std::discrete_distribution<uint64_t> hotspot_dist(hotspot_weights.begin(), hotspot_weights.end());

        keys.clear();
        keys.reserve(key_count);

        uint64_t total_keys = num_ / FLAGS_worker_threads;
        if (tid == FLAGS_worker_threads - 1) {
            total_keys += num_ % FLAGS_worker_threads;
        }

        uint64_t thread_start_key_idx = tid * (num_ / FLAGS_worker_threads);
        uint64_t thread_end_key_idx = thread_start_key_idx + key_count;

        std::vector<std::vector<uint64_t>> hotspot_keys(num_hotspots + 1);

        for (uint64_t key_idx = thread_start_key_idx; key_idx < thread_end_key_idx; ++key_idx) {
            uint64_t hotspot_id = key_idx / keys_per_hotspot;

            if (hotspot_id >= num_hotspots) {
                std::uniform_int_distribution<uint64_t> uniform_dist(0, key_space_size - 1);
                hotspot_keys[num_hotspots].push_back(uniform_dist(gen));
                continue;
            }

            uint64_t hotspot_start = hotspot_id * hotspot_size;

            uint64_t offset_in_hotspot = hotspot_dist(gen);  // [0, hotspot_size-1]
            uint64_t key = hotspot_start + offset_in_hotspot;

            hotspot_keys[hotspot_id].push_back(key);
        }

        std::mt19937 shuffle_gen(fixed_seed + tid + 0x1000);
        keys.clear();
        keys.reserve(key_count);

        for (uint64_t hotspot_id = 0; hotspot_id <= num_hotspots; ++hotspot_id) {
            if (!hotspot_keys[hotspot_id].empty()) {
                std::shuffle(hotspot_keys[hotspot_id].begin(), hotspot_keys[hotspot_id].end(), shuffle_gen);
                keys.insert(keys.end(), hotspot_keys[hotspot_id].begin(), hotspot_keys[hotspot_id].end());
            }
        }
    }

    void PrepareShuffledKeys() {
        shuffled_keys_.resize(FLAGS_worker_threads);

        std::string dist_type = FLAGS_key_distribution;
        if (dist_type != "uniform" && dist_type != "zipfian" && dist_type != "hotspot_zipfian") {
            std::cerr << "Error: Invalid key_distribution: " << dist_type
                      << ". Must be 'uniform', 'zipfian', or 'hotspot_zipfian'" << std::endl;
            exit(1);
        }

        uint64_t keys_per_thread = num_ / FLAGS_worker_threads;
        uint64_t remainder = num_ % FLAGS_worker_threads;

        std::cerr << "Preparing shuffled key arrays for " << FLAGS_worker_threads << " threads..." << std::endl;
        std::cerr << "Key distribution: " << dist_type << std::endl;
        std::cerr << "Key order directory: " << FLAGS_key_order_dir << std::endl;

        bool all_loaded = true;

        for (uint32_t tid = 0; tid < FLAGS_worker_threads; ++tid) {
            uint64_t expected_count = keys_per_thread;
            if (tid == FLAGS_worker_threads - 1) {
                expected_count += remainder;
            }

            if (LoadKeysFromFile(tid, shuffled_keys_[tid])) {
                if (shuffled_keys_[tid].size() != expected_count) {
                    std::cerr << "Warning: Thread " << tid << " loaded " << shuffled_keys_[tid].size()
                              << " keys, but expected " << expected_count << ". Regenerating..." << std::endl;
                    shuffled_keys_[tid].clear();
                    all_loaded = false;
                }
            } else {
                all_loaded = false;
            }
        }

        if (all_loaded) {
            std::cerr << "All key orders loaded from files. Using fixed key order." << std::endl;
            return;
        }

        std::cerr << "Generating new key orders..." << std::endl;

        const uint64_t fixed_seed = 0x1234567890ABCDEFULL;

        for (uint32_t tid = 0; tid < FLAGS_worker_threads; ++tid) {
            uint64_t thread_key_count = keys_per_thread;
            if (tid == FLAGS_worker_threads - 1) {
                thread_key_count += remainder;
            }

            if (shuffled_keys_[tid].empty()) {
                if (dist_type == "uniform") {
                    shuffled_keys_[tid].reserve(thread_key_count);

                    uint64_t start_key = tid * keys_per_thread + 1;
                    uint64_t end_key = start_key + thread_key_count - 1;

                    for (uint64_t key = start_key; key <= end_key; ++key) {
                        shuffled_keys_[tid].push_back(key);
                    }

                    std::string sort_type = FLAGS_key_sort;
                    if (sort_type == "ascending") {
                        std::cerr << "Thread " << tid << ": generated " << thread_key_count
                                  << " keys (uniform, range [" << start_key << ", " << end_key << "], ascending)" << std::endl;
                    } else if (sort_type == "descending") {
                        std::reverse(shuffled_keys_[tid].begin(), shuffled_keys_[tid].end());
                        std::cerr << "Thread " << tid << ": generated " << thread_key_count
                                  << " keys (uniform, range [" << start_key << ", " << end_key << "], descending)" << std::endl;
                    } else {
                        uint64_t shuffle_seed = fixed_seed + tid;

                        if (sort_type.find("random_seed") == 0) {
                            std::string seed_str = sort_type.substr(11);
                            if (!seed_str.empty()) {
                                try {
                                    uint64_t seed_offset = std::stoull(seed_str);
                                    shuffle_seed = fixed_seed + tid + (seed_offset * 0x1000000ULL);
                                } catch (...) {
                                }
                            }
                        }

                        std::mt19937 g(shuffle_seed);
                        std::shuffle(shuffled_keys_[tid].begin(), shuffled_keys_[tid].end(), g);

                        std::cerr << "Thread " << tid << ": generated " << thread_key_count
                                  << " keys (uniform, range [" << start_key << ", " << end_key << "], sort=" << sort_type << ")" << std::endl;
                    }
                } else if (dist_type == "zipfian") {
                    GenerateZipfianKeys(tid, thread_key_count, shuffled_keys_[tid]);

                    uint64_t key_space_size = num_ / 4;
                    if (key_space_size == 0) key_space_size = 1;
                    std::cerr << "Thread " << tid << ": generated " << thread_key_count
                              << " keys (zipfian, key space [0, " << key_space_size << "])" << std::endl;
                } else if (dist_type == "hotspot_zipfian") {
                    GenerateHotspotZipfianKeys(tid, thread_key_count, shuffled_keys_[tid]);

                    std::cerr << "Thread " << tid << ": generated " << thread_key_count
                              << " keys (hotspot_zipfian, 60 hotspots, key space [0, 60000000])" << std::endl;
                }

                if (!SaveKeysToFile(tid, shuffled_keys_[tid])) {
                    std::cerr << "Warning: Failed to save key order for thread " << tid << std::endl;
                }
            }
        }
    }

    void DoSimpleRandomWrite (ThreadState* thread) {

        rocksdb::WriteBatch wb;
        rocksdb::WriteOptions writeDisableWAL;
        writeDisableWAL.disableWAL = true;

        uint64_t base_keys = 0;
        const char* base_keys_env = getenv("ROCKSDB_BASE_KEYS");
        if (base_keys_env != nullptr) {
            base_keys = std::strtoull(base_keys_env, nullptr, 10);
        }

        const std::vector<uint64_t>& keys = shuffled_keys_[thread->tid];
        size_t writes_per_thread = keys.size();

        std::random_device rd;
        std::mt19937 generator(rd() ^ (std::chrono::system_clock::now().time_since_epoch().count() + thread->tid * 0x9e3779b9));
        std::uniform_int_distribution<uint64_t> value_seed_dist(0, UINT64_MAX);

        thread->stats.Start ();
        thread->stats.SetTotalOps (writes_per_thread);

        size_t ind = 0;
        uint64_t success_count = 0;
        const uint64_t print_interval = 10000;
        uint64_t last_success_count = 0;
        uint64_t last_check_time = NowMicros();

        while (ind < writes_per_thread) {
            uint64_t j = 0;
            for (; j < FLAGS_batch && ind < writes_per_thread; j++, ind++) {
                uint64_t key = keys[ind];

                Key k;
                TransformKey (key, k);

                uint64_t value_seed = value_seed_dist(generator) ^
                                      (std::chrono::system_clock::now().time_since_epoch().count() + ind);
                std::string rValue;
                rValue.resize (value_size_);
                std::memcpy (const_cast<char*>(rValue.data ()), &value_seed, sizeof(value_seed));
                if (static_cast<size_t>(value_size_) > sizeof(value_seed)) {
                    std::memcpy (const_cast<char*>(rValue.data()) + sizeof(value_seed), &key, sizeof(key));
                }

                wb.Put(rocksdb::Slice ((char*)k.getData (), k.getKeyLen ()), rValue);

                if (wb.Count() == FLAGS_batch) {
                    rocksdb::Status s = db1->Write(writeDisableWAL, &wb);
                    size_t batch_count = wb.Count();

                    if (s.ok()) {
                        success_count += batch_count;
                    } else {
                        fprintf (stderr, "Thread %d: Write ERROR at count %llu: %s\n",
                                thread->tid, (unsigned long long)success_count, s.ToString().c_str());
                        fflush (stderr);
                    }

                    wb.Clear();
                    uint32_t delay_ms = (FLAGS_add_delay > 0) ? FLAGS_add_delay : 1;
                    std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));

                    if (s.ok() && success_count % print_interval == 0 && success_count > 0) {
                        std::string timestamp = GetTimestamp();
                        std::string disk_usage = GetDiskUsage();
                        uint64_t total_keys = base_keys + success_count;
                        fprintf (stderr, "Thread %d: %llu writes completed (batch_count=%lu)\n",
                                thread->tid, (unsigned long long)success_count, batch_count);
                        fprintf (stderr, "[write-progress] timestamp=%s completed_keys=%llu disk_usage=%s\n",
                                timestamp.c_str(), (unsigned long long)total_keys, disk_usage.c_str());
                        fflush (stderr);
                    }

                    if (!s.ok()) {
                        fprintf (stderr, "Thread %d: Write FAILED at success_count=%llu: %s (batch_count=%lu)\n",
                                thread->tid, (unsigned long long)success_count,
                                s.ToString().c_str(), batch_count);
                        fflush (stderr);
                    }

                    uint64_t now = NowMicros();
                    if (now - last_check_time > 5000000) {
                        if (success_count == last_success_count && success_count > 0) {
                            fprintf (stderr, "Thread %d: WARNING - success_count stuck at %llu for 5+ seconds! "
                                    "Last Write status=%s, batch_count=%lu, current wb.Count()=%u\n",
                                    thread->tid, (unsigned long long)success_count,
                                    s.ok() ? "OK" : s.ToString().c_str(), batch_count, (unsigned int)wb.Count());
                            fflush (stderr);
                        }
                        last_success_count = success_count;
                        last_check_time = now;
                    }
                }
            }

            (void)thread->stats.FinishedBatchOp (j);
            if (thread->tid == 0 && thread->stats.ShouldPrintRocksDBStats ()) {
                PrintRocksDBStats ();
            }
        }

        if (wb.Count() > 0) {
            rocksdb::Status s = db1->Write(writeDisableWAL, &wb);
            size_t batch_count = wb.Count();

            if (s.ok()) {
                success_count += batch_count;
            } else {
                fprintf (stderr, "Thread %d: Final Write ERROR at count %llu: %s (batch_count=%lu)\n",
                        thread->tid, (unsigned long long)success_count,
                        s.ToString().c_str(), batch_count);
                fflush (stderr);
            }
                    wb.Clear();

            if (success_count % print_interval == 0 && success_count > 0) {
                std::string timestamp = GetTimestamp();
                std::string disk_usage = GetDiskUsage();
                uint64_t total_keys = base_keys + success_count;
                fprintf (stderr, "Thread %d: %llu writes completed (final flush, batch_count=%lu, status=%s)\n",
                        thread->tid, (unsigned long long)success_count,
                        batch_count, s.ok() ? "OK" : "ERROR");
                fprintf (stderr, "[write-progress] timestamp=%s completed_keys=%llu disk_usage=%s\n",
                        timestamp.c_str(), (unsigned long long)total_keys, disk_usage.c_str());
                fflush (stderr);
            }
        }

        fprintf (stderr, "Thread %d: Total %llu writes completed\n",
                thread->tid, (unsigned long long)success_count);

        thread->stats.PrintProgressBar ();
        fprintf (stderr, "\n");
    }

    void PrintKeySpaceInfo () {
        fprintf (stdout, "================================================\n");
        fprintf (stdout, "Key Space Information:\n");
        if (FLAGS_key_distribution == "uniform") {
            fprintf (stdout, "  Key space range:     [1, %lu]\n", (uint64_t)num_);
        } else if (FLAGS_key_distribution == "zipfian") {
            uint64_t key_space_size = num_ / 4;
            fprintf (stdout, "  Key space range:     [0, %lu] (zipfian distribution)\n", (uint64_t)key_space_size);
        } else if (FLAGS_key_distribution == "hotspot_zipfian") {
            fprintf (stdout, "  Key space range:     [0, 60000000] (60 hotspots, zipfian within each hotspot)\n");
        } else {
            fprintf (stdout, "  Key space range:     [1, %lu]\n", (uint64_t)num_);
        }
        fprintf (stdout, "  Total writes:        %lu\n", (uint64_t)num_);
        fprintf (stdout, "  Value size:          %lu bytes\n", (uint64_t)value_size_);
        fprintf (stdout, "================================================\n");
        fflush (stdout);
    }

    void PrintRocksDBStats () {
        if (db1 == nullptr) {
            return;
        }
        std::string stats;
        bool ret = db1->GetProperty ("rocksdb.stats", &stats);
        if (ret) {
            fprintf (stdout, "\n========== RocksDB Statistics ==========\n");
            fprintf (stdout, "%s", stats.c_str ());
            fprintf (stdout, "=========================================\n\n");
            fflush (stdout);
        }
        PrintApplicationWriteAmplification();
    }

    void PrintApplicationWriteAmplification () {
        if (statistics_ == nullptr) {
            return;
        }
        uint64_t user_bytes = statistics_->getTickerCount(rocksdb::BYTES_WRITTEN);
        uint64_t flush_bytes = statistics_->getTickerCount(rocksdb::FLUSH_WRITE_BYTES);
        uint64_t compact_bytes = statistics_->getTickerCount(rocksdb::COMPACT_WRITE_BYTES);
        double write_amp = (user_bytes > 0)
            ? static_cast<double>(flush_bytes + compact_bytes) / static_cast<double>(user_bytes)
            : 0.0;
        fprintf (stdout, "========== Application Write Amplification ==========\n");
        fprintf (stdout, "  User bytes written (ingest):  %" PRIu64 " (%.2f MB)\n",
                 user_bytes, user_bytes / (1024.0 * 1024.0));
        fprintf (stdout, "  Flush bytes written:          %" PRIu64 " (%.2f MB)\n",
                 flush_bytes, flush_bytes / (1024.0 * 1024.0));
        fprintf (stdout, "  Compaction bytes written:     %" PRIu64 " (%.2f MB)\n",
                 compact_bytes, compact_bytes / (1024.0 * 1024.0));
        fprintf (stdout, "  Application write amplification: %.2f ( (flush + compact) / user )\n",
                 write_amp);
        fprintf (stdout, "=====================================================\n\n");
        fflush (stdout);
    }

    void PrintHeader () {
        fprintf (stdout, "------------------------------------------------\n");
        fprintf (stdout, "Shuffled Sequential Write Benchmark\n");
        fprintf (stdout, "Key distribution:      %s\n", FLAGS_key_distribution.c_str());
        if (FLAGS_key_distribution == "uniform") {
            fprintf (stdout, "Key space:             [1, %lu]\n", (uint64_t)num_);
        } else if (FLAGS_key_distribution == "zipfian") {
            uint64_t key_space_size = num_ / 4;
            fprintf (stdout, "Key space:             [0, %lu] (zipfian distribution)\n", (uint64_t)key_space_size);
        } else if (FLAGS_key_distribution == "hotspot_zipfian") {
            fprintf (stdout, "Key space:             [0, 60000000] (60 hotspots, zipfian within each hotspot)\n");
        }
        fprintf (stdout, "Total writes:          %lu\n", (uint64_t)num_);
        fprintf (stdout, "Value size:            %lu bytes\n", (uint64_t)value_size_);
        fprintf (stdout, "Threads:               %u\n", FLAGS_worker_threads);
        fprintf (stdout, "RocksDB batch:         %u\n", FLAGS_batch);
        fprintf (stdout, "------------------------------------------------\n");
        fflush (stdout);
    }

private:
    struct ThreadArg {
        Benchmark* bm;
        SharedState* shared;
        ThreadState* thread;
        void (Benchmark::*method) (ThreadState*);
    };

    static void ThreadBody (void* v) {
        ThreadArg* arg = reinterpret_cast<ThreadArg*> (v);
        SharedState* shared = arg->shared;
        ThreadState* thread = arg->thread;

        {
            std::unique_lock<std::mutex> lck (shared->mu);
            shared->num_initialized++;
            if (shared->num_initialized >= shared->total) {
                shared->cv.notify_all ();
            }
            while (!shared->start) {
                shared->cv.wait (lck);
            }
        }


        thread->stats.Start ();
        (arg->bm->*(arg->method)) (thread);
        thread->stats.Stop ();

        {
            std::unique_lock<std::mutex> lck (shared->mu);
            shared->num_done++;
            if (shared->num_done >= shared->total) {
                shared->cv.notify_all ();
            }
        }
    }

    void RunBenchmark (int thread_num, const std::string& name,
                       void (Benchmark::*method) (ThreadState*)) {

        SharedState shared (thread_num);
        ThreadArg* arg = new ThreadArg[thread_num];
        std::thread server_threads[thread_num];

        for (int i = 0; i < thread_num; i++) {
            arg[i].bm = this;
            arg[i].method = method;
            arg[i].shared = &shared;
            arg[i].thread = new ThreadState (i);
            arg[i].thread->shared = &shared;
            server_threads[i] = std::thread (ThreadBody, &arg[i]);
        }

        std::unique_lock<std::mutex> lck (shared.mu);
        while (shared.num_initialized < thread_num) {
            shared.cv.wait (lck);
        }

        shared.start = true;
        shared.cv.notify_all ();
        while (shared.num_done < thread_num) {
            shared.cv.wait (lck);
        }

        for (int i = 1; i < thread_num; i++) {
            arg[0].thread->stats.Merge (arg[i].thread->stats);
        }
        arg[0].thread->stats.Report (name);

        for (auto& th : server_threads) th.join ();

        for (int i = 0; i < thread_num; i++) {
            delete arg[i].thread;
        }
        delete[] arg;
    }
};

}  // namespace

int main (int argc, char* argv[]) {
    ParseCommandLineFlags (&argc, &argv, true);
    Benchmark benchmark;
    benchmark.Run ();
    return 0;
}
