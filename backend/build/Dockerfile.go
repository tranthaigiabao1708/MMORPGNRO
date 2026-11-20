# Stage 1: Static build for Go 1.23+ service binaries (VS-01, without VS-02 api/gen)
FROM golang:1.23.3-bookworm AS builder

ARG SERVICE_NAME=gameapi
WORKDIR /src

# Cache module dependencies before copying source packages
COPY go.mod go.sum ./
RUN go mod download && go mod verify

COPY pkg/ ./pkg/
COPY internal/ ./internal/
COPY cmd/ ./cmd/

RUN CGO_ENABLED=0 GOOS=linux GOARCH=amd64 \
    go build -trimpath -ldflags="-s -w" \
    -o /out/service ./cmd/${SERVICE_NAME}

# Stage 2: Minimal distroless runtime image (nonroot UID/GID 65532, no shell)
FROM gcr.io/distroless/static-debian12:nonroot

WORKDIR /app
COPY --from=builder /out/service /app/service

USER 65532:65532
ENTRYPOINT ["/app/service"]
