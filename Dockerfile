FROM alpine:latest

RUN apk add --no-cache \
    libmicrohttpd-dev \
    curl-dev \
    gcc \
    make \
    musl-dev

WORKDIR /app

COPY main.c Makefile ./

RUN make

RUN apk add --no-cache libmicrohttpd curl

EXPOSE 8080

CMD ["./server"]