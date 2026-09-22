#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

uint64_t ParseSize(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  uint64_t multiplier = 1;
  std::string number = value;
  const char unit = value.back();
  if (unit < '0' || unit > '9') {
    number.pop_back();
    switch (unit) {
      case 'K':
      case 'k':
        multiplier = 1ULL << 10;
        break;
      case 'M':
      case 'm':
        multiplier = 1ULL << 20;
        break;
      case 'G':
      case 'g':
        multiplier = 1ULL << 30;
        break;
      case 'T':
      case 't':
        multiplier = 1ULL << 40;
        break;
      default:
        return 0;
    }
  }
  try {
    return std::stoull(number) * multiplier;
  } catch (...) {
    return 0;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0] << " SIZE OUTPUT_FILE\n";
    return 2;
  }
  const uint64_t target = ParseSize(argv[1]);
  if (target == 0 || target % 4096 != 0) {
    std::cerr << "SIZE must be a positive multiple of 4096 bytes.\n";
    return 2;
  }

  const int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_DIRECT, 0644);
  if (fd < 0) {
    std::cerr << "Cannot create " << argv[2] << ": " << std::strerror(errno)
              << "\n";
    return 1;
  }

  constexpr size_t kBufferSize = 4 * 1024 * 1024;
  void* buffer = nullptr;
  if (posix_memalign(&buffer, 4096, kBufferSize) != 0) {
    std::cerr << "Aligned allocation failed.\n";
    close(fd);
    unlink(argv[2]);
    return 1;
  }
  std::memset(buffer, 0x55, kBufferSize);

  uint64_t written = 0;
  const auto started = std::chrono::steady_clock::now();
  while (written < target) {
    const size_t size =
        static_cast<size_t>(std::min<uint64_t>(kBufferSize, target - written));
    const ssize_t result = write(fd, buffer, size);
    if (result <= 0) {
      std::cerr << "Write failed after " << written << " bytes: "
                << std::strerror(errno) << "\n";
      free(buffer);
      close(fd);
      return 1;
    }
    written += static_cast<uint64_t>(result);
    if (written % (1ULL << 30) == 0 || written == target) {
      const double percent = 100.0 * written / target;
      std::cout << "\rPrefill " << std::fixed << std::setprecision(1) << percent
                << "%" << std::flush;
    }
  }

  const int sync_result = fsync(fd);
  free(buffer);
  close(fd);
  if (sync_result != 0) {
    std::cerr << "\nfsync failed: " << std::strerror(errno) << "\n";
    return 1;
  }
  const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::steady_clock::now() - started)
                           .count();
  std::cout << "\nWrote " << written << " bytes in " << seconds << " seconds.\n";
  return 0;
}
