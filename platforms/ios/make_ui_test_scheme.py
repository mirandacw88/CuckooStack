#!/usr/bin/env python3
"""Writes the shared Xcode schemes that build, run and UI-test the editor (CMake makes the CuckooEditorUITests target but no scheme
with a test action). Run by generate_xcode.sh after CMake: make_ui_test_scheme.py <build dir>.
  xcodebuild test -project <build dir>/CuckooStack.xcodeproj -scheme CuckooEditorUITests -destination 'id=<simulator>'"""
import re
import sys
from pathlib import Path

PRODUCTS = {"app": "CuckooEditor", "tests": "CuckooEditorUITests"} # (CMake files each product under its bare name)


def target_ids(pbxproj: str) -> dict:
    """target name -> its object id (the PBXNativeTarget entries)"""
    ids = {}
    for m in re.finditer(r"\t\t([0-9A-F]{24}) /\* (\w+) \*/ = \{\n\t\t\tisa = PBXNativeTarget;", pbxproj):
        ids[m.group(2)] = m.group(1)
    return ids


def entry(ident: str, product: str, name: str, container: str, flags: str) -> str:
    return f"""      <BuildActionEntry {flags}>
         <BuildableReference BuildableIdentifier = "primary" BlueprintIdentifier = "{ident}" BuildableName = "{product}" BlueprintName = "{name}" ReferencedContainer = "container:{container}"></BuildableReference>
      </BuildActionEntry>
"""


def scheme(ids: dict, container: str) -> str:
    app, tests = ids["CuckooEditor"], ids["CuckooEditorUITests"]
    ref = lambda i, p, n: f'<BuildableReference BuildableIdentifier = "primary" BlueprintIdentifier = "{i}" BuildableName = "{p}" BlueprintName = "{n}" ReferencedContainer = "container:{container}"></BuildableReference>'
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<Scheme LastUpgradeVersion = "1600" version = "1.7">
   <BuildAction parallelizeBuildables = "YES" buildImplicitDependencies = "YES">
      <BuildActionEntries>
{entry(app, PRODUCTS["app"], "CuckooEditor", container, 'buildForTesting = "YES" buildForRunning = "YES" buildForProfiling = "YES" buildForArchiving = "YES" buildForAnalyzing = "YES"')}{entry(tests, PRODUCTS["tests"], "CuckooEditorUITests", container, 'buildForTesting = "YES" buildForRunning = "NO" buildForProfiling = "NO" buildForArchiving = "NO" buildForAnalyzing = "NO"')}      </BuildActionEntries>
   </BuildAction>
   <TestAction buildConfiguration = "Debug" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" shouldUseLaunchSchemeArgsEnv = "NO">
      <MacroExpansion>
         {ref(app, PRODUCTS["app"], "CuckooEditor")}
      </MacroExpansion>
      <Testables>
         <TestableReference skipped = "NO">
            {ref(tests, PRODUCTS["tests"], "CuckooEditorUITests")}
         </TestableReference>
      </Testables>
   </TestAction>
   <LaunchAction buildConfiguration = "Debug" selectedDebuggerIdentifier = "Xcode.DebuggerFoundation.Debugger.LLDB" selectedLauncherIdentifier = "Xcode.DebuggerFoundation.Launcher.LLDB" launchStyle = "0" useCustomWorkingDirectory = "NO" ignoresPersistentStateOnLaunch = "NO" debugDocumentVersioning = "YES" allowLocationSimulation = "YES">
      <BuildableProductRunnable runnableDebuggingMode = "0">
         {ref(app, PRODUCTS["app"], "CuckooEditor")}
      </BuildableProductRunnable>
   </LaunchAction>
</Scheme>
"""


def main(build: Path) -> int:
    proj = build / "CuckooStack.xcodeproj"
    ids = target_ids((proj / "project.pbxproj").read_text())
    if "CuckooEditorUITests" not in ids or "CuckooEditor" not in ids:
        print("no CuckooEditorUITests target in the project: no UI test scheme")
        return 0
    # (the container as CMake's own schemes name it: the project's path from the source folder)
    container = f"{proj.resolve().relative_to(Path(__file__).resolve().parents[2])}"
    out = proj / "xcshareddata" / "xcschemes"
    out.mkdir(parents=True, exist_ok=True)
    # (the editor's own scheme too: Xcode makes none for a target a shared scheme already names, so without it the
    # project has no CuckooEditor scheme to build and run; Run launches the editor, Test runs its UI tests)
    for name in ("CuckooEditor", "CuckooEditorUITests"):
        (out / f"{name}.xcscheme").write_text(scheme(ids, container))
        print(f"wrote {out / (name + '.xcscheme')}")
    return 0


if __name__ == "__main__":
    sys.exit(main(Path(sys.argv[1])))
