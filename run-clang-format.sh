#!/bin/sh
for SRC_DIR in src test tools
do
  find $SRC_DIR -name '*.h' -or -name '*.cc' | xargs clang-format -i -style file
done

