if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    CC=g++
elif [[ "$OSTYPE" == "darwin"* ]]; then
    CC=clang
else
    echo "Unknown operating system \"$OSTYPE\"."
    exit 1
fi

mkdir -p build
"$CC" -g -std=c++20 code/main.cpp -o build/indexer
"$CC" -g -std=c++20 code/test.cpp -o build/test
