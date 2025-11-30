#!/bin/bash
set -e

cd "$(dirname "$0")"

# Check if server already running
if curl -s http://localhost:8000/health &>/dev/null; then
    echo "Server already running on :8000"
    exit 0
fi

# Build backend server
echo "Building server..."
go build -o server server.go

# Start server
echo "Starting server on :8000..."
exec ./server

