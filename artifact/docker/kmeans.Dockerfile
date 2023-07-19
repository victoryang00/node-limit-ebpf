FROM ubuntu:latest
RUN mkdir -p /bede-data/workloads
WORKDIR /bede-data/workloads/Kmeans
COPY . /bede-data/workloads/Kmeans
CMD ["./test.sh"]