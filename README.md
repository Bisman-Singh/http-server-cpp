# HTTP Server

A minimal HTTP/1.1 server written in C++ using POSIX sockets. Serves static files from a local `www/` directory.

## Features

- HTTP/1.1 GET request handling
- Serves static files from `www/` directory
- Content-Type detection based on file extension (html, css, js, json, txt, png, jpg)
- 404 response for missing files
- Handles multiple concurrent connections via `fork()`
- Configurable port via CLI argument (default: 8080)
- Path sanitization to prevent directory traversal
- Graceful shutdown on Ctrl+C

## Build

```bash
make
```

## Usage

```bash
./httpserver          # Start on default port 8080
./httpserver 3000     # Start on port 3000
```

Then open `http://localhost:8080` in your browser.

## Project Structure

```
http-server/
├── main.cpp
├── Makefile
├── README.md
└── www/
    └── index.html
```
