# Glue for using MeshCore as a library dependency:
# - it bundles ed25519 in its own lib/ folder, which PlatformIO doesn't build for library users
# - its helpers include the board variant's target.h, which lives in our include/
Import("env")
d = env.subst("$PROJECT_LIBDEPS_DIR/$PIOENV/MeshCore/lib/ed25519")
env.Append(CPPPATH=[d, "$PROJECT_INCLUDE_DIR"])
env.BuildSources("$BUILD_DIR/ed25519", d)
