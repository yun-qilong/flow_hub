pipeline {
  agent any
  stages {
    stage('Checkout') {
      steps {
        checkout([$class: 'GitSCM',
          branches: [[name: 'FETCH_HEAD']],
          userRemoteConfigs: [[refspec: params.GERRIT_REFSPEC ?: 'refs/heads/main', url: 'ssh://qilyun@localhost:29418/flow_hub']],
          extensions: [[$class: 'RelativeTargetDirectory', relativeTargetDir: 'src']]
        ])
      }
    }
    stage('Issue Check') {
      steps {
        catchError(buildResult: 'FAILURE', stageResult: 'FAILURE') {
          dir('src') {
            sh 'bash scripts/check-issue-ref.sh --strict FETCH_HEAD'
          }
        }
      }
    }
    stage('Build') {
      steps {
        dir('src') {
          sh 'cmake -B build -DCMAKE_BUILD_TYPE=Release -DFLOWHUB_BUILD_TESTS=ON && cmake --build build --target flowhub flowhub_ut -j $(nproc)'
        }
      }
    }
    stage('Test') {
      steps {
        dir('src/build') { sh 'ctest --output-on-failure -L flowhub' }
      }
    }
    stage('Format') {
      steps {
        dir('src') {
          sh '''#!/bin/bash
FILES=$(find . -name "*.cpp" -o -name "*.hpp" | grep -v "/generated/" | grep -v "/build/" | sort)
for f in $FILES; do
  [ -f "$f" ] || continue
  if ! /usr/bin/clang-format-15 --dry-run --Werror "$f" 2>/dev/null; then
    echo ""; echo "==== FORMAT ISSUES: $f ===="
    diff -u "$f" <(/usr/bin/clang-format-15 "$f") 2>/dev/null || true
  fi
done
echo "Format check complete"'''
        }
      }
    }
    stage('Tidy') {
      steps {
        dir('src') {
          sh '''echo "=== clang-tidy diagnostics ==="
which clang-tidy-14 || echo "clang-tidy-14 NOT FOUND"
clang-tidy-14 --version || echo "version check failed"
echo "================================"
CLANG_TIDY_BIN=clang-tidy-14 python3 scripts/run_tidy.py'''
        }
      }
    }
  }
}
