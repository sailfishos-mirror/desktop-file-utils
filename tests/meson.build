intent_cache_test = find_program('intent-cache.sh')

build_root = meson.project_build_root()

test('intent-cache', intent_cache_test,
  depends: update_desktop_database,
  env: [f'BUILD_ROOT=@build_root@'],
)