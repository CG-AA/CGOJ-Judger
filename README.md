# CGOJ-Judger

CGOJ-Judger is a lightweight, high-performance online judge system component designed to handle and evaluate code submissions. It leverages modern C++ libraries and tools to ensure efficient and reliable performance.

## Libraries and Tools

- **MicroHTTPD**: Used for handling HTTP requests and responses.
- **nlohmann/json**: A JSON library for modern C++ to parse and handle JSON data.
- **spdlog**: A fast C++ logging library.
- **Isolate**: A sandboxing tool to securely execute untrusted code.

## Features

- **Memory only**(todo): The judger is designed to be memory-only, meaning that it does not store any data on disk.
- **Sandboxing**: The judger uses the Isolate sandboxing tool to execute untrusted code securely.

## Installation
```bash
docker build -t cgoj-judger .
docker run -d -p 45803:45803 cgoj-judger
```

## License
Check [LICENSE](LICENSE) for more information.