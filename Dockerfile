FROM ubuntu
USER root

WORKDIR /asn1-codec

# Add build tools.
RUN apt-get update && apt-get install -y software-properties-common wget git make gcc-4.9 g++-4.9 gcc-4.9-base && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.9 100 && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-4.9 100

# Install cmake.
RUN wget https://cmake.org/files/v3.7/cmake-3.7.2.tar.gz && tar -xvf cmake-3.7.2.tar.gz
RUN cd cmake-3.7.2 && ./bootstrap && make && make install && cd /home

# install libtool and automake
RUN apt-get update && apt-get install -y automake libtool

# Install librdkafka.
ADD ./librdkafka /asn1-codec/librdkafka
RUN cd /asn1-codec/librdkafka && ln -s /usr/bin/python3 /usr/bin/python && ./configure && make && make install

# Install pugixml
ADD ./pugixml /asn1-codec/pugixml
RUN cd /asn1-codec/pugixml && mkdir -p build && cd build && cmake .. && make && make install

# Build and install asn1c submodule
ADD ./asn1c /asn1-codec/asn1c
RUN cd /asn1-codec/asn1c && acloacl && test -f configure || autoreconf -iv && ./configure && make && make install

# Generate ASN.1 API.
RUN export LD_LIBRARY_PATH=/usr/local/lib
ADD ./asn1c_combined /asn1-codec/asn1c_combined
ADD ./scms-asn /asn1-codec/scms-asn
RUN cd /asn1-codec/asn1c_combined && bash doIt.sh

# add the source and build files
ADD CMakeLists.txt /asn1-codec
ADD ./config /asn1-codec/config
ADD ./include /asn1-codec/include
ADD ./src /asn1-codec/src
ADD ./kafka-test /asn1-codec/kafka-test
ADD ./unit-test-data /asn1-codec/unit-test-data

# Build acm.
RUN cd /asn1-codec && mkdir -p /build && cd /build && cmake /asn1-codec && make

# Add test data. This changes frequently so keep it low in the file.
ADD ./docker-test /asn1-codec/docker-test

CMD ["/asn1-codec/docker-test/acm.sh"]
