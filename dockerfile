FROM ubuntu:20.04 AS build

# Set environment variables to configure tzdata non-interactively
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

# Install dependencies and isolate
RUN apt-get update && apt-get install -y \
    build-essential \
    git \
    cmake \
    libcap-dev \
    pkg-config \
    libsystemd-dev \
    nlohmann-json3-dev \
    curl \
    libfmt-dev \
    libcrypto++-dev \
    && rm -rf /var/lib/apt/lists/* \
    && git clone https://github.com/ioi/isolate.git /tmp/isolate \
    && cd /tmp/isolate \
    && make isolate 

# Install libmicrohttpd
RUN curl -sL https://mirror.ossplanet.net/gnu/libmicrohttpd/libmicrohttpd-latest.tar.gz | tar xz -C /tmp \
    && cd /tmp/libmicrohttpd-* \
    && ./configure \
    && make \
    && make install \
    && rm -rf /tmp/libmicrohttpd-*

# Install spdlog
RUN git clone https://github.com/gabime/spdlog.git \
    && cd spdlog && mkdir build && cd build \
    && cmake .. && make -j \
    && make install \
    && rm -rf /spdlog

# Copy source code
COPY main.cpp /usr/src/main.cpp

# Compile the application (c++17)
RUN g++ /usr/src/main.cpp -o /usr/local/bin/app -I/usr/local/lib -static -static-libgcc -static-libstdc++ -lmicrohttpd -lspdlog -lfmt -lpthread -lcryptopp -std=c++17

# Runtime stage
FROM ubuntu:20.04 AS runtime

# Set environment variables to configure tzdata non-interactively
# Prevent tzdata from asking for the timezone
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=Etc/UTC

RUN apt-get update && apt-get install -y \
    nginx \
    make \
    pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Install isolate
COPY --from=build /tmp/isolate /tmp/isolate
RUN cd /tmp/isolate \
    && make install \
    && rm -rf /tmp/isolate

# Copy nginx configuration
COPY nginx.conf /etc/nginx/nginx.conf

# Copy the compiled application and settings
COPY --from=build /usr/local/bin/app /usr/local/bin/app
COPY settings.json /usr/local/bin/settings.json

EXPOSE 45803

CMD cd /usr/local/bin && ./app
# COPY --from=build /usr/src/main.cpp /usr/src/main.cpp

#docker build -t cgjg .
#docker run -p 45803:45803 cgjg