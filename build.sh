mkdir -p build
clang++ -g -std=c++20 code/main.cpp -o build/indexer
clang++ -g -std=c++20 code/test.cpp -o build/test
