# Shared helpers for the Sessionist build scripts. Sourced, not run.

# update_clone <dir> <url> <branch>: clone if missing, otherwise fast-forward
# when the clone is clean. Local edits or commits are left alone.
update_clone() {
  local dir="$1" url="$2" branch="$3"
  if [ ! -d "$dir/.git" ]; then
    git clone --branch "$branch" --recursive "$url" "$dir"
    return
  fi
  if ! git -C "$dir" diff --quiet --ignore-submodules HEAD; then
    echo "warning: $dir has local edits; not updating it"
  elif git -C "$dir" pull --ff-only --quiet; then
    git -C "$dir" submodule update --init --recursive || echo "warning: submodule update failed in $dir"
  else
    echo "warning: could not fast-forward $dir; using what is checked out"
  fi
  git -C "$dir" status -sb | head -1
}
