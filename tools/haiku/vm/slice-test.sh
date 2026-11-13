#!/usr/bin/env bash
# slice-test.sh ARCH [PRINTER]
#
# The headless slicer's acceptance test, in an air/OS guest (run-vm.sh):
# stages bambu-studio-cli from the build tree with the resources it needs and
# the test models, then in the guest lists the presets, slices
#
#   - resources/model/Bambu_Cube_V2.stl (a 20 mm cube)
#   - tests/data/test_3mf/Geräte/Büchse.3mf (a 3MF with a non-ASCII path)
#   - tests/data/20mm_cube.obj and Open CASCADE's sample screw.step (the OBJ
#     and STEP importers)
#   - resources/model/ksr_FDMTest.stl (the bigger FDM test piece; skipped
#     with SLICE_TEST_QUICK=1, which is the default for the emulated arm64)
#
# with a stock printer profile (default "Bambu Lab A1 0.4 nozzle" and its
# default process and filament) and exports each as plate_1.gcode and a
# .gcode.3mf; before that it prints the platform layer's findings
# (--platform-info) and runs minicv's self-test. Without SLICE_TEST_QUICK the
# cube is sliced again with 1 and 2 threads (TBB scaling). Checks on the host what came back: the G-code has layers,
# filament use and an estimated time, and the 3MF contains it.
#
# Results: BS_SCRATCH/vm-core/slice-test-ARCH/out (output.log, the G-code
# and 3MF files, summary.txt).
set -euo pipefail
HERE=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
ARCH=${1:?usage: slice-test.sh x86_64|arm64 [printer preset]}
PRINTER=${2:-Bambu Lab A1 0.4 nozzle}
. "$HERE/../env.sh" "$ARCH"
QUICK=${SLICE_TEST_QUICK:-$([[ $HAIKU_ARCH == arm64 ]] && echo 1 || echo 0)}

cli=$BS_BUILD/src/bambu-studio-cli
[[ -x $cli ]] || { echo "slice-test.sh: no $cli (run tools/haiku/build.sh $HAIKU_ARCH)" >&2; exit 1; }
payload=$BS_SCRATCH/vm-core/slice-test-$HAIKU_ARCH
rm -rf "$payload"
mkdir -p "$payload/BambuStudio/bin" "$payload/BambuStudio/resources" "$payload/models"
"${CROSS}strip" --strip-debug -o "$payload/BambuStudio/bin/bambu-studio-cli" "$cli"
# minicv's self-test (the OpenCV subset libslic3r uses), if it was built
cmake --build "$BS_BUILD" --target minicv_test > /dev/null
cp "$BS_BUILD/src/minicv/minicv_test" "$payload/BambuStudio/bin/"
# what a slice reads of the resources (the rest is for the GUI)
for dir in profiles profiles_template info printers flush filament_mixing data; do
	cp -rL "$BS_ROOT/resources/$dir" "$payload/BambuStudio/resources/"
done
cp "$BS_ROOT/resources/model/Bambu_Cube_V2.stl" "$BS_ROOT/resources/model/ksr_FDMTest.stl" "$payload/models/"
cp "$BS_ROOT/tests/data/test_3mf/Geräte/Büchse.3mf" "$BS_ROOT/tests/data/20mm_cube.obj" "$payload/models/"
# a STEP file: Open CASCADE's own sample, if its source is unpacked
step_sample=$(ls "$BS_DEPS_SRC"/occt-*/data/step/screw.step 2>/dev/null | head -n 1)
[[ -z $step_sample ]] || cp "$step_sample" "$payload/models/"

cat > "$payload/run.sh" <<RUN
CLI=/payload/BambuStudio/bin/bambu-studio-cli
PRINTER="$PRINTER"
QUICK=$QUICK
status=0
step() {
	echo "=== \$*"
	"\$@"
	code=\$?
	echo "=== exit \$code"
	[ \$code -eq 0 ] || status=1
}
uname -a
sysinfo -cpu | head -n 2
sysinfo -mem | head -n 3
step \$CLI --help
step \$CLI --platform-info
step /payload/BambuStudio/bin/minicv_test
step sh -c "\$CLI --list-presets | grep -c ."
step sh -c "\$CLI --list-presets --printer \"\$PRINTER\" | head -n 12"
step \$CLI --debug 2 --printer "\$PRINTER" --info --slice 1 --outputdir /payload/out/cube \\
	--export-3mf cube.gcode.3mf /payload/models/Bambu_Cube_V2.stl
step \$CLI --debug 2 --printer "\$PRINTER" --info --slice 1 --outputdir /payload/out/buechse \\
	--export-3mf "Büchse.gcode.3mf" "/payload/models/Büchse.3mf"
step \$CLI --debug 2 --printer "\$PRINTER" --info --slice 1 --outputdir /payload/out/obj /payload/models/20mm_cube.obj
if [ -f /payload/models/screw.step ]; then
	step \$CLI --debug 2 --printer "\$PRINTER" --info --slice 1 --outputdir /payload/out/step /payload/models/screw.step
fi
if [ \$QUICK -eq 0 ]; then
	step \$CLI --debug 2 --printer "\$PRINTER" --info --orient 1 --slice 1 --outputdir /payload/out/fdmtest \\
		--export-3mf fdmtest.gcode.3mf /payload/models/ksr_FDMTest.stl
	step \$CLI --debug 2 --threads 1 --printer "\$PRINTER" --slice 1 --outputdir /payload/out/cube-1thread \\
		/payload/models/Bambu_Cube_V2.stl
	step \$CLI --debug 2 --threads 2 --printer "\$PRINTER" --slice 1 --outputdir /payload/out/cube-2threads \\
		/payload/models/Bambu_Cube_V2.stl
fi
exit \$status
RUN

status=0
"$HERE/run-vm.sh" "$HAIKU_ARCH" "$payload" || status=$?

# --- what came back
out=$payload/out
summary=$out/summary.txt
{
	echo "slice test, $HAIKU_ARCH, printer \"$PRINTER\", guest exit status $status"
	grep -a "^sliced in" "$out/output.log" || true
	for gcode in "$out"/*/plate_1.gcode; do
		[[ -f $gcode ]] || continue
		name=$(basename "$(dirname "$gcode")")
		layers=$(grep -c "^; CHANGE_LAYER" "$gcode" || true)
		echo "$name: $(stat -c %s "$gcode") bytes, $layers layer changes"
		grep -m1 "^; total layer number" "$gcode" || true
		grep -m1 "^; total estimated time\|^; model printing time" "$gcode" || true
		grep -m1 "^; total filament length\|^; filament used \[mm\]" "$gcode" || true
		grep -m1 "^; total filament weight\|^; filament used \[g\]" "$gcode" || true
		[[ $layers -gt 0 ]] || { echo "$name: NO LAYERS"; status=1; }
	done
	for project in "$out"/*/*.gcode.3mf; do
		[[ -f $project ]] || continue
		listing=$(unzip -l "$project")
		if grep -q "Metadata/plate_1.gcode" <<< "$listing"; then
			echo "$(basename "$project"): $(stat -c %s "$project") bytes, has Metadata/plate_1.gcode"
		else
			echo "$(basename "$project"): NO G-CODE INSIDE"
			status=1
		fi
	done
	[[ -n $(ls "$out"/*/plate_1.gcode 2>/dev/null) ]] || { echo "no G-code came back"; status=1; }
	echo "RESULT: $([[ $status == 0 ]] && echo PASS || echo FAIL)"
} | tee "$summary"
exit $status
