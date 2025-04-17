FROM cpp-build-base:0.1.0 AS build
WORKDIR /work

COPY CMakeLists.txt ./
ADD src ./src
ADD drivers ./drivers
ADD assets ./assets
 
RUN mkdir plugins && cmake . &&  make
#RUN   ls -al /work/plugins

FROM dtcooper/raspberrypi-os:bookworm

RUN apt-get update \
   && apt-get -y --no-install-recommends install \
        git-core \
#        sqlite3 \
        libsqlite3-dev \
#        i2c-tools \
        gpiod \
        libgpiod-dev \
#        curl \
        libcurl4-openssl-dev \
#        nano \
   && rm -rf /var/lib/apt/lists/*

COPY --from=build /work/piotserver .
RUN  mkdir ./plugins
COPY --from=build /work/plugins  .
RUN  rm *.so
#RUN  ls -l ./plugins

COPY --from=build /work/assets/piotserver.props.json .
ENV TZ="America/Chicago"
 
