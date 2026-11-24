"""Run the actual combined shell entry points against disposable filesystem fixtures."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import time
from zipfile import ZipFile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data.encode() if isinstance(data, str) else data)
    path.chmod(0o755)


OPS = """#!/bin/sh
case "$1" in
  sha256) value=$(sha256sum "$2") && echo "${value%% *}";;
  same) cmp -s "$2" "$3";;
  version) grep -qx model_firmware "$2";;
  *) exit 2;;
esac
"""
MOUNT = "/dev/root / ext4 ro 0 0\n/dev/mmcblk0p14 /opt/usr ext4 rw 0 0\n/dev/mmcblk1p1 /opt/storage/sdcard exfat rw 0 0\n"
BOOT = {"proc/sys/kernel/random/boot_id":"first\n","sys/power/bootmode":"2\n",
        "sys/class/extcon/USB/state":"0\n","proc/mounts":MOUNT,"proc/swaps":"Filename Type Size Used Priority\n"}
MODEL = """task_root=.
sync(){ :; }
stat(){
  if [ "$1" = -c ] && [ "$2" = %d ]; then
    if [ "$3" = ./opt/usr ]; then echo 14; else echo 10; fi
  else command stat "$@"; fi
}
"""


def shell_run(shell, root, filename):
    env = dict(os.environ, PATH="/usr/bin:/bin" if os.name != "nt" else os.environ["PATH"])
    return subprocess.run([shell,"-c",'PATH=/usr/bin:/bin; export PATH; /bin/sh "$1"',"model",filename],
                          cwd=root, env=env, capture_output=True, timeout=150)


def model_worker(bundle, old_eye, old_mod, shell, case, want):
    with tempfile.TemporaryDirectory(prefix="nx040-") as work:
        root = Path(work)
        pkg = root/"mnt/mmc/nxl-full040"
        sd = pkg.parent
        mod, state, rt = [root/p for p in ("opt/usr/nx-lut-lite","opt/usr/nxl-full040","usr/local/lib/nxl-full040")]
        drop = root/"etc/systemd/system/di-camera-app.service.d"
        live = sd/"nx-on-wake/on-wake"
        shutil.copytree(bundle/"nxl-full040",pkg)
        for name in ("install.sh","info.tg","nx_cs.adj"):
            write(sd/name,(pkg/"activation"/name).read_bytes())
        for name, data in BOOT.items():
            write(root/name,data)
        for name in ("tmp","usr/local/lib","usr/local/bin","etc/systemd/system","opt/usr","mnt/mmc/nx-on-wake"):
            (root/name).mkdir(parents=True, exist_ok=True)
        write(root/"etc/version.info","model_firmware\n")
        write(root/"etc/snapshot","yes")
        write(root/"dev/console","")
        write(root/"bin/mount","#!/bin/sh\necho \"mount $*\" >> sequence\n")
        sysctl = ('#!/bin/sh\necho "ctl $*" >> sequence\n'
                  'if [ "$1" = show ]; then echo "ExecStart=/bin/sh /usr/local/lib/nxl-full040/launch.sh"; fi\n')
        write(root/"usr/bin/systemctl",sysctl)
        prog = (pkg/"worker.sh").read_text("utf8").replace("task_root=\n",MODEL)
        # Replace only the firmware-fixture identities. Packaged scripts and module
        # manifests still execute their own comparisons with real file hashes.
        stock = re.findall(r'pin "\$(?:task_root([^"]+)|SYSTEMCTL)" ([0-9a-f]{64})',prog)
        stock += [("/usr/lib/libmmfcore.so","b3e7cd2987282d856a8f95b8768d44ea2f4f3804684a391de0567c10dada73c8"),
                  ("/bin/sleep","8adde8f159709200cac6a4731c55438d343164ad0327b1fbc85c1c3b52ae9d16")]
        for path,pin in stock:
            dest = root/(path.lstrip("/") or "usr/bin/systemctl")
            if not dest.exists():
                write(dest,"firmware fixture "+path)
            prog = prog.replace(pin,sha(dest.read_bytes()))
        write(pkg/"fileops.arm",OPS)
        endpoints = {"nxl-menu":"#!/bin/sh\necho notice >> sequence\ntest ! -f reject-notice\n",
                     "claim.arm":'#!/bin/sh\nprintf "file=%s\\n" "$6" > "$1/launch-claim"\n',
                     "loader-observe.sh":'#!/bin/sh\ntest -f "$1" && test -f "$2" && test ! -f reject-loader\n',
                     "dispatch.sh":(pkg/"dispatch.sh").read_text().replace("task_root=\n","task_root=.\n")}
        for name,stub in endpoints.items():
            real = sha((pkg/name).read_bytes())
            if real not in prog:
                raise ValueError(name)
            write(pkg/name,stub)
            prog = prog.replace(real,sha((pkg/name).read_bytes()))
        prog = prog.replace("cd / || fail FINAL_CWD","cd . || fail FINAL_CWD").replace(">/dev/console",'>"$task_root/dev/console"')
        write(pkg/"worker.sh",prog)
        policy = {"stock-check":"CHECK","original-check":"CHECK","inspect":"INSPECT","clear":"CLEAR"}.get(case,"EYE")
        write(pkg/"request",policy+"\n")
        with ZipFile(old_eye) as z:
            prior = z.read("nxl-eye-image28/wake-entry")
            conf = z.read("nxl-eye-image28/capture.conf")
        if case != "stock-check":
            write(live,prior)
            write(drop/"67-nxl-eye-publication.conf",conf)
        if case == "clear":
            write(state/"previous-on-wake",prior)
            write(state/"armed","old\nEYE\n")
            write(rt/"launch.sh",(pkg/"launch.sh").read_bytes())
            write(drop/"68-nxl-full040.conf",(pkg/"capture.conf").read_bytes())
            write(live,(pkg/"wake-entry").read_bytes())
        if case == "inspect":
            write(state/"menu-status","bundle=0.4.0\nresult=LISTENER_INPUTS_OPEN\n")
        if case == "foreign":
            write(drop/"67-nxl-eye-publication.conf","unrecognized")
        if case == "busy":
            write(root/"tmp/nx-lut-lite/gamma-r11h.lease","occupied")
        if case == "low-battery":
            write(root/"sys/class/power_supply/battery/capacity","35\n")
        if case in ("reject-loader","reject-notice"):
            write(root/case,"1")
        if case not in ("clear","inspect","foreign","busy","missing"):
            source = bundle/"module/nx-lut-demo"
            if case == "original-check":
                with ZipFile(old_mod) as z:
                    manifest = z.read("nx-lut-demo/module.manifest")
                    for line in manifest.decode().splitlines():
                        _, name = line.split()
                        write(mod/name,z.read("nx-lut-demo/nx-lut-lite/"+name))
            else:
                manifest = (source/"module.manifest").read_bytes()
                for row in manifest.decode().splitlines():
                    _, name = row.split()
                    dest = mod/name
                    dest.parent.mkdir(parents=True,exist_ok=True)
                    shutil.copyfile(source/"nx-lut-lite"/name,dest)
            write(mod/"state/module.manifest",manifest)
            write(mod/"state/demo51-installed","NXDEMO51_INSTALLED_V1\ninstall_revision=release-0.4.0-rc6.14.12-log117\n")
            if case == "bad-file":
                write(mod/"MODULE.id","damaged")
            if case == "bad-revision":
                write(mod/"state/demo51-installed","other\n")
        run = shell_run(shell,root,"mnt/mmc/nxl-full040/worker.sh")
        result, = (sd/"NXDEMO51").iterdir()
        reason = (result/"result.txt").read_text().split("reason=",1)[1].strip()
        sequence = (root/"sequence").read_text() if (root/"sequence").exists() else ""
        if reason != want:
            raise AssertionError((case,reason,want,run.stderr.decode(errors="replace")[-300:]))
        assert (result/"entry/info.tg").is_file() and not (sd/"info.tg").exists()
        assert ("ctl rescue" in sequence) == (case == "v28-upgrade")
        if case == "v28-upgrade":
            assert (rt/"full-menu.sh").read_bytes() == (pkg/"full-menu.sh").read_bytes()
            assert (rt/"nxl-eye-af.so").read_bytes() == (pkg/"nxl-eye-af.so").read_bytes()
            assert (state/"previous-on-wake").read_bytes() == prior and (state/"armed").is_file()
            assert live.read_bytes() == (pkg/"wake-entry").read_bytes()
        elif case != "stock-check":
            assert live.read_bytes() == prior, case
        if case == "inspect":
            assert (result/"full-menu.txt").read_bytes() == (state/"menu-status").read_bytes()
        if case == "clear":
            assert not rt.exists() and not (drop/"68-nxl-full040.conf").exists()
        if case != "stock-check":
            assert (drop/"67-nxl-eye-publication.conf").read_bytes() == (b"unrecognized" if case=="foreign" else conf)
        return case


def model_menu(bundle,shell,case,want):
    with tempfile.TemporaryDirectory(prefix="nx040-menu-") as work:
        root=Path(work)
        mod=root/"opt/usr/nx-lut-lite"
        state=root/"opt/usr/nxl-full040"
        leaf="FAST_START_EYE_AF_ROOTLIB_1_2"
        report=root/"mnt/mmc/NXDEMO51"/leaf/"resume"
        for name,data in BOOT.items():
            write(root/name,data)
        write(root/"tmp/.hib_post","1")
        write(state/"armed","first\nEYE\n"+leaf+"\n")
        write(state/"fileops.arm",OPS)
        report.parent.mkdir(parents=True)
        manifest=(bundle/"module/nx-lut-demo/module.manifest").read_bytes()
        write(mod/"state/module.manifest",manifest)
        stub='#!/bin/sh\necho start >> started\nmkdir -p tmp/nx-lut-lite proc/24\necho 24 > tmp/nx-lut-lite/listener.ready\necho /opt/usr/nx-lut-lite/bin/nxl-keys > proc/24/exe\n'
        if case=="start-failed": stub+='exit 8\n'
        if case=="wrong-process": stub+='echo /other > proc/24/exe\n'
        write(mod/"bt-start.sh",stub)
        prog=(bundle/"nxl-full040/full-menu.sh").read_text().replace("task_root=\n",'task_root=.\nreadlink(){ cat "$1"; }\n')
        pin=sha((bundle/"module/nx-lut-demo/nx-lut-lite/bt-start.sh").read_bytes())
        assert pin in prog
        write(root/"entry.sh",prog.replace(pin,sha(stub.encode())))
        if case=="usb": write(root/"sys/class/extcon/USB/state","1")
        if case=="capture": write(root/"opt/etc/.hib_capturing","1")
        if case=="no-resume": (root/"tmp/.hib_post").unlink()
        if case=="swap": write(root/"proc/swaps","Filename Type Size Used Priority\n/dev/mmcblk0p9 partition 1 0 1\n")
        if case=="bad-manifest": write(mod/"state/module.manifest","bad")
        if case=="disabled": write(mod/"state/disabled","1")
        if case=="already-live":
            write(root/"tmp/nx-lut-lite/listener.ready","24\n")
            write(root/"proc/24/exe","/opt/usr/nx-lut-lite/bin/nxl-keys\n")
        shell_run(shell,root,"entry.sh")
        result=state/"menu-status"
        assert (result.read_text().split("result=",1)[1].strip() if result.exists() else None)==want,case
        if want is not None: assert (report/"full-menu.txt").read_bytes()==result.read_bytes()
        assert (root/"started").exists() == (case in ("fresh","start-failed","wrong-process"))
        if case=="fresh":
            shell_run(shell,root,"entry.sh")
            assert (root/"started").read_text().splitlines()==["start"]
        return case


def model_combined_wake(bundle,shell):
    with tempfile.TemporaryDirectory(prefix="nx040-combined-") as work:
        root=Path(work)
        runtime=root/"usr/local/lib/nxl-full040"
        state=root/"opt/usr/nxl-full040"
        mod=root/"opt/usr/nx-lut-lite"
        pkg=bundle/"nxl-full040"
        leaf="FAST_START_EYE_AF_ROOTLIB_1_2"
        report=root/"mnt/mmc/NXDEMO51"/leaf/"resume"
        for name,data in BOOT.items(): write(root/name,data)
        report.parent.mkdir(parents=True)
        write(root/"tmp/.hib_post","1")
        write(state/"armed","before\nEYE\n"+leaf+"\n")
        write(state/"fileops.arm",OPS)
        eye="/usr/local/lib/nxl-full040/nxl-eye-af.so"
        write(state/"launch-claim",f"boot_id=first\ncamera_pid=251\nfile={eye}\nmapped_dev=b3:0a\nmapped_ino=224\n")
        write(root/"proc/251/maps",f"aa-bb r-xp 00001000 b3:0a 224 {eye}\n")
        write(root/"tmp/nx-lut-lite/eye-af.status","NXL_EYE_AF2 pid=251 state=ACTIVE eye_frames=1 discarded_publications=0 schema=28\n")
        write(mod/"state/module.manifest",(bundle/"module/nx-lut-demo/module.manifest").read_bytes())
        stub='#!/bin/sh\nmkdir -p proc/24\nprintf "24\\n" > tmp/nx-lut-lite/listener.ready\necho /opt/usr/nx-lut-lite/bin/nxl-keys > proc/24/exe\n'
        write(mod/"bt-start.sh",stub)
        menu=(pkg/"full-menu.sh").read_text("utf8")
        menu=menu.replace("task_root=\n",'task_root=.\nreadlink(){ cat "$1"; }\n')
        pin=sha((bundle/"module/nx-lut-demo/nx-lut-lite/bt-start.sh").read_bytes())
        write(runtime/"full-menu.sh",menu.replace(pin,sha(stub.encode())))
        observer=(pkg/"readout").read_text("utf8")
        observer=observer.replace("task_root=\n",'''task_root=.
dmesg(){ echo 'kernel_resume start!'; }
readlink(){ echo /usr/apps/com.samsung.di-camera-app/bin/di-camera-app; }
sleep(){ if [ -f tmp/nx-lut-lite/eye-af.resume-ready ] && [ -d proc/251 ]; then mv proc/251 proc/end; fi; }
''')
        observer=observer.replace(sha((pkg/"full-menu.sh").read_bytes()),sha((runtime/"full-menu.sh").read_bytes()))
        write(runtime/"readout",observer)
        result=shell_run(shell,root,"usr/local/lib/nxl-full040/readout")
        assert result.returncode==0
        for _ in range(240):
            if (report/"full-menu.txt").is_file() and (report/"eye-hit.txt").is_file() and (root/"proc/end").is_dir(): break
            time.sleep(0.05)
        assert "same_capture_process=1\nresult=ADAPTER_ENABLED_AFTER_RESTORE" in (report/"runtime.log").read_text()
        assert "schema=28" in (report/"eye-hit.txt").read_text()
        assert (report/"full-menu.txt").read_text().splitlines()==["bundle=0.4.0","result=LISTENER_INPUTS_OPEN"]
        return "same resume yielded the eye, process and menu receipts"


def run(bundle,eye,original,shell):
    workers={"stock-check":"CHECKED_READY","original-check":"CHECKED_READY","v28-upgrade":"OEM_RESCUE_REQUEST",
             "inspect":"INSPECTION_COMPLETE","clear":"OWNED_CAPTURE_DISABLED","foreign":"FOREIGN_PUBLICATION_OVERRIDE",
             "busy":"CAMERA_NOT_IDLE_OR_SNAPSHOT_UNAVAILABLE","missing":"FULL_MODULE_INSTALL_REQUIRED",
             "bad-file":"FULL_MODULE_FILE_MISMATCH","bad-revision":"FULL_MODULE_INSTALL_REQUIRED",
             "low-battery":"FULL_BATTERY_REQUIRED","reject-loader":"STOCK_PROCESS_LIBRARY_CHECK_FAILED","reject-notice":"NOTICE_NOT_SHOWN"}
    menus={"fresh":"LISTENER_INPUTS_OPEN","already-live":"LISTENER_INPUTS_OPEN","usb":None,"capture":None,"no-resume":None,
           "swap":None,"disabled":"MODULE_DISABLED","bad-manifest":"MODULE_IDENTITY_FAILED",
           "start-failed":"LISTENER_START_FAILED","wrong-process":"LISTENER_UNCONFIRMED"}
    with ThreadPoolExecutor(max_workers=3) as pool:
        jobs=[pool.submit(model_worker,bundle,eye,original,shell,k,v) for k,v in workers.items()]
        jobs.extend(pool.submit(model_menu,bundle,shell,k,v) for k,v in menus.items())
        for job in jobs: print("PASS",job.result(),flush=True)
    combined=model_combined_wake(bundle,shell)
    print("PASS",combined,flush=True)
    return {"worker":list(workers),"menu":list(menus),"combined":combined,"camera_endpoints":"modeled"}


if __name__=="__main__":
    cli=argparse.ArgumentParser(description=__doc__)
    for name in ("bundle","eye_zip","original_module_zip"): cli.add_argument(name,type=Path)
    cli.add_argument("--shell",default="sh")
    cli.add_argument("--report",type=Path)
    a=cli.parse_args()
    result=run(a.bundle.resolve(),a.eye_zip,a.original_module_zip,a.shell)
    if a.report: a.report.write_text(json.dumps(result,indent=2)+"\n",encoding="utf8")
