#!/bin/sh
cd "$(dirname "$0")"
for SRC_DIR in src test tools
do
  find $SRC_DIR -name '*.h' -or -name '*.inl' -or -name '*.cc' | xargs clang-format -i -style file
done

