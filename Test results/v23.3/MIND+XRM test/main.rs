import subprocess
import os

project_dir = "/teamspace/studios/this_studio/XRM-SSD V23.3/xrm_ssd_v23_3_integration"

env = os.environ.copy()
env['PATH'] = f"{os.environ['HOME']}/.cargo/bin:{env['PATH']}"

#Use cargo fix to automatically fix
print("Run cargo fix...")
result = subprocess.run(
["cargo", "fix", "--bin", "xrm_ssd_v23_3_integration", "--allow-dirty"],
cwd=project_dir,
env=env,
capture_output=True,
text=True
)

print(result.stdout)
if result.stderr:    print(result.stderr)

print("\nRerun...")
result2 = subprocess.run(
["cargo", "run", "--release"],
cwd=project_dir,
env=env,
capture_output=True,
text=True
)

print(result2.stdout)
