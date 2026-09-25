#include "goxx_generator.h"

#include "lexer.h"
#include "parser.h"
#include "resolver.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static std::string ReadFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

static int CheckTs(const char* path) {
  std::string source = ReadFile(path);
  if (source.empty()) {
    std::fprintf(stderr, "ts_goxx_frontend_test: cannot read %s\n", path);
    return 1;
  }
  std::string goxx = bruja::GenerateGoxx(source, path);
  bool ok = goxx.find("--backend=goxx") != std::string::npos &&
            goxx.find(".goxx marks TypeScript converted to Go++") != std::string::npos &&
            goxx.find("package main") != std::string::npos &&
            goxx.find("import \"fmt\"") != std::string::npos &&
            goxx.find("type Point struct") != std::string::npos &&
            goxx.find("x float64") != std::string::npos &&
            goxx.find("func greet(name string) string") != std::string::npos &&
            goxx.find("return Point{x: 0, y: 0}") != std::string::npos &&
            goxx.find("type Counter struct") != std::string::npos &&
            goxx.find("func NewCounter()") != std::string::npos &&
            goxx.find("func (this *Counter) inc() float64") != std::string::npos &&
            goxx.find("func main()") != std::string::npos &&
            goxx.find("c := NewCounter()") != std::string::npos &&
            goxx.find("fmt.Println(greet(\"bruja\"))") != std::string::npos &&
            goxx.find(".go") != std::string::npos;
  if (!ok) {
    std::fprintf(stderr, "ts_goxx_frontend_test FAIL goxx=\n%s\n", goxx.c_str());
    return 1;
  }
  std::printf("ts_goxx_frontend_test: OK ts %s\n", path);
  return 0;
}

static int CheckBruja(const char* path) {
  std::string source = ReadFile(path);
  if (source.empty()) {
    std::fprintf(stderr, "ts_goxx_frontend_test: cannot read %s\n", path);
    return 1;
  }
  bruja::Module module = bruja::Parse(bruja::Tokenize(source));
  bruja::Resolve(module);
  std::string goxx = bruja::GenerateGoxxFromModule(module, path, "console");
  bool ok = goxx.find("package console") != std::string::npos &&
            goxx.find("type Console interface") != std::string::npos &&
            goxx.find("Log(message string)") != std::string::npos &&
            goxx.find("Warn(message string)") != std::string::npos &&
            goxx.find(".goxx marks converted Go++") != std::string::npos;
  if (!ok) {
    std::fprintf(stderr, "ts_goxx_frontend_test FAIL idl goxx=\n%s\n", goxx.c_str());
    return 1;
  }
  std::printf("ts_goxx_frontend_test: OK bruja %s\n", path);
  return 0;
}

int main(int argc, char** argv) {
  const char* ts = argc > 1 ? argv[1] : "examples/goxx/hello.ts";
  const char* bruja = argc > 2 ? argv[2] : "examples/console/console.bruja";
  if (CheckTs(ts) != 0) return 1;
  if (CheckBruja(bruja) != 0) return 1;
  return 0;
}
