FROM alpine:latest

RUN apk update && apk add --no-cache \
    build-base \
    nginx \
    isolate \
    && rm -rf /var/cache/apk/*

# Configure isolate
RUN mkdir -p /var/local/isolate/0 && \
    isolate --init

# Copy nginx configuration
COPY nginx.conf /etc/nginx/nginx.conf

COPY app.cpp /usr/src/app.cpp

RUN g++ /usr/src/app.cpp -o /usr/local/bin/app

EXPOSE 45803

CMD ["sh", "-c", "nginx && /usr/local/bin/app"]

#docker build -t CGJG .
#docker run -p 45803:45803 CGJG