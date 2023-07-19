FROM ubuntu:latest
RUN mkdir -p /bede-data/workloads
WORKDIR /bede-data/workloads/GAPBS
COPY . /bede-data/workloads/GAPBS
CMD ["./test.sh"]