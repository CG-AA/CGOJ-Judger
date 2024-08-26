FROM ubuntu:latest

RUN apt-get update
RUN apt-get install -y \
    build-essential \
    nginx \
    pkg-config \
    libcap-dev \
    libsystemd-dev \
    nlohmann-json3-dev \
    libmicrohttpd-dev \
    libspdlog-dev \
    git \
    asciidoc \
    && rm -rf /var/lib/apt/lists/*

# Install isolate
RUN git clone https://github.com/ioi/isolate.git /tmp/isolate \
    && cd /tmp/isolate \
    && make \
    && make install \
    && rm -rf /tmp/isolate

# Configure isolate
RUN mkdir -p /var/local/isolate/0 && \
    isolate --init

# Copy nginx configuration
COPY nginx.conf /etc/nginx/nginx.conf

COPY main.cpp /usr/src/main.cpp

RUN g++ /usr/src/main.cpp -o /usr/local/bin/app

EXPOSE 45803

CMD ["sh", "-c", "nginx && /usr/local/bin/app"]

#docker build -t cgjg .
#docker run -p 45803:45803 cgjg