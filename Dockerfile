FROM alpine:latest AS builder

RUN apk add --no-cache gcc musl-dev libmicrohttpd-dev curl-dev

COPY main.c .

RUN gcc -Wall -O3 -std=c99 -o server main.c -lmicrohttpd -lcurl

FROM alpine:latest

RUN apk add --no-cache libmicrohttpd curl

COPY --from=builder server .

EXPOSE 8080

CMD ["./server"]