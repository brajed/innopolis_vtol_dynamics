#!/bin/bash
cd "$(dirname "$0")"
source config.sh

sudo docker container run --rm -it                                  \
                          $FLAGS_FOR_DEV                            \
                          $DOCKERHUB_REPOSITOTY:$TAG_NAME           \
                          /bin/bash
