FROM alpine:latest

RUN apk update && apk add --no-cache \
    build-base \
    nginx \
    isolate \
    nlohmann-json3-dev \
    libmicrohttpd-dev \
    libspdlog-dev \
    && rm -rf /var/cache/apk/*

# Configure isolate
RUN mkdir -p /var/local/isolate/0 && \
    isolate --init

# Copy nginx configuration
COPY nginx.conf /etc/nginx/nginx.conf

COPY main.cpp /usr/src/main.cpp

RUN g++ /usr/src/main.cpp -o /usr/local/bin/app

EXPOSE 45803

CMD ["sh", "-c", "nginx && /usr/local/bin/main"]

#docker build -t cgjg .
#docker run -p 45803:45803 cgjg