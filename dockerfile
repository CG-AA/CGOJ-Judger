FROM ubuntu:20.04 AS build

RUN ls

# Set environment variables to configure tzdata non-interactively
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Install dependencies and isolate
RUN apt-get update && apt-get install -y \
    asciidoc \
    build-essential \
    git \
    libcap-dev \
    pkg-config \
    libsystemd-dev \
    nlohmann-json3-dev \
    libspdlog-dev \
    curl \
    libfmt-dev \
    && rm -rf /var/lib/apt/lists/* \
    && git clone https://github.com/ioi/isolate.git /tmp/isolate \
    && cd /tmp/isolate \
    && make \
    && make install \
    && rm -rf /tmp/isolate

# innstall libmicrohttpd
RUN curl -sL https://mirror.ossplanet.net/gnu/libmicrohttpd/libmicrohttpd-latest.tar.gz | tar xz -C /tmp \
    && cd /tmp/libmicrohttpd-* \
    && ./configure \
    && make \
    && make install \
    && rm -rf /tmp/libmicrohttpd-*

# Configure isolate
RUN mkdir -p /var/local/isolate/0 && \
    isolate --init

# Copy source code
COPY main.cpp /usr/src/main.cpp

# Compile the application
RUN g++ /usr/src/main.cpp -o /usr/local/bin/app -lmicrohttpd -lspdlog -lfmt

# Runtime stage
FROM ubuntu:20.04

# Set environment variables to configure tzdata non-interactively
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

RUN apt-get update && apt-get install -y \
    nginx \
    libmicrohttpd-dev \
    libspdlog-dev \
    && rm -rf /var/lib/apt/lists/*

# Copy nginx configuration
COPY nginx.conf /etc/nginx/nginx.conf

# Copy the compiled application and settings
COPY --from=build /usr/local/bin/app /usr/local/bin/app
COPY settings.json /usr/local/bin/settings.json

# Expose the application port
EXPOSE 45803

# Start nginx and the application
CMD cd /usr/local/bin && ./app

#docker build -t cgjg .
#docker run -p 45803:45803 cgjg