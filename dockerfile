FROM ubuntu:latest

RUN apt-get update --fix-missing
RUN apt-get install -y \
    build-essential \
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
COPY settings.json /usr/src/settings.json

RUN g++ /usr/src/main.cpp -o /usr/local/bin/app -lmicrohttpd -lspdlog -lfmt

EXPOSE 45803

CMD ["/usr/local/bin/app"]

#docker build -t cgjg .
#docker run -p 45803:45803 cgjg