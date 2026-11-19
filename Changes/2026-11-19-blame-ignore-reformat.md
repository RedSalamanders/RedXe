- `.git-blame-ignore-revs` lists the reformat of pull request #108 (the shared 155-line `.clang-format`) and its
  merge commit, so `git blame` with `blame.ignoreRevsFile` and GitHub's blame view attribute lines to the change
  before the reformat. The commit that moved one C4191 suppression is not listed, because it changes a line.
  This is the follow-up of step CV-6-05 of the `RedHome` convergence plan (register D-01).
