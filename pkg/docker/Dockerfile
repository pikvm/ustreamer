FROM debian:buster-slim as build
ARG TARGETPLATFORM
WORKDIR /build/ustreamer
COPY .github/workflows/install-build-dependencies.sh .
RUN ./install-build-dependencies.sh
COPY . .
RUN ./build.sh

FROM debian:buster-slim
ARG TARGETPLATFORM
WORKDIR /ustreamer
COPY .github/workflows/install-runtime-dependencies.sh .
RUN ./install-runtime-dependencies.sh && rm install-runtime-dependencies.sh
COPY --from=build /build/ustreamer/ustreamer .
EXPOSE 8080
ENTRYPOINT ["./ustreamer"]
