#!/usr/bin/env python3
"""Real failed-report regression plus synthetic diagnostic fault injections."""
from pathlib import Path
import json,shutil,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from analyze_p21a_report import analyze
import test_p21_report as models
HEADER='P21A_PREFLIGHT_V1; same p21 predicates; read-only gate diagnostics\n'
STATE='GATE_RESULT type=STATE call=1 pass=1 reason=PASS round=0 index=0 address=0x0 expected=0x0 actual=0x0 errno=0\n'
REJECT='GATE_RESULT type=IDLE call=1 pass=0 reason=RAW_PENDING round=0 index=1 address=0x10cc expected=0x0 actual=0x20 errno=0\n'
SUMMARY='GATE_SUMMARY state_calls=1 idle_calls=1 rejections=1 last_type=IDLE last_reason=RAW_PENDING hardware_jobs=0 mmio_modified=0 safe_to_release=1\n'
class Diagnostics(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.r=Path(self.tmp.name)/'report';shutil.copytree(ROOT/'evidence/p21-failed',self.r)
 def tearDown(self):self.tmp.cleanup()
 def synthetic_log(self):
  root=json.loads((self.r/'result.json').read_text());root['version']='0.3.4-p21a';(self.r/'result.json').write_text(json.dumps(root))
  (self.r/'p21a_preflight.txt').write_text(HEADER+STATE+REJECT+SUMMARY)
 def test_real_failed_p21_report(self):
  r=analyze(self.r);self.assertEqual(r['verdict'],'PREFLIGHT_REJECTED');self.assertFalse(r['jpeg_encoder_tested']);self.assertTrue(r['cleanup_verified']);self.assertFalse(r['rejection_identified'])
 def test_diagnostic_reason_identified_without_claiming_jpeg_success(self):
  self.synthetic_log();r=analyze(self.r);self.assertEqual(r['verdict'],'PREFLIGHT_REJECTED');self.assertFalse(r['jpeg_encoder_tested']);self.assertTrue(r['rejection_identified']);self.assertEqual(r['preflight_diagnostics']['rejections'][0]['address'],0x10cc)
 def test_incomplete_diagnostic_log(self):
  self.synthetic_log();p=self.r/'p21a_preflight.txt';p.write_text(HEADER+STATE+REJECT);r=analyze(self.r);self.assertFalse(r['rejection_identified']);self.assertNotEqual(r['verdict'],'NATIVE_JPEG_WRITEBACK_VERIFIED')
 def test_inconsistent_summary(self):
  self.synthetic_log();p=self.r/'p21a_preflight.txt';p.write_text(p.read_text().replace('rejections=1','rejections=0'));self.assertFalse(analyze(self.r)['rejection_identified'])
 def test_kernel_change_blocks_cleanup_claim(self):
  b=bytearray((self.r/'p11_kernel_after.bin').read_bytes());b[8]^=1;(self.r/'p11_kernel_after.bin').write_bytes(b);r=analyze(self.r);self.assertFalse(r['cleanup_verified']);self.assertEqual(r['verdict'],'NOT_VERIFIED')
 def test_false_release_claim_rejected_by_csv(self):
  p=self.r/'allocator.csv';text=p.read_text();lines=text.splitlines();fields=lines[-1].split(',');fields[7]='0x00000023';fields[8]='8';lines[-1]=','.join(fields);p.write_text('\n'.join(lines)+'\n');self.assertFalse(analyze(self.r)['cleanup_verified'])
 def test_worker_corruption(self):
  p=self.r/'copy_worker.bin';b=bytearray(p.read_bytes());b[12]^=1;p.write_bytes(b)
  with self.assertRaises(ValueError):analyze(self.r)
 def test_wrong_diagnostic_header(self):
  self.synthetic_log();(self.r/'p21a_preflight.txt').write_text('foreign log')
  with self.assertRaises(ValueError):analyze(self.r)
class P21AFullImage(models.Report):
 def setUp(self):super().setUp();self.root['version']='0.3.4-p21b';self.save_root()
 def test_wrapper_keeps_full_image_success_checks(self):
  good='GATE_RESULT type=IDLE call=1 pass=1 reason=PASS round=16 index=0 address=0x0 expected=0x0 actual=0x0 errno=0\n'
  summary='GATE_SUMMARY state_calls=1 idle_calls=1 rejections=0 last_type=NONE last_reason=NONE hardware_jobs=1 mmio_modified=1 safe_to_release=1\n'
  (self.r/'p21a_preflight.txt').write_text(HEADER+STATE+good+summary)
  self.assertEqual(analyze(self.r)['verdict'],'NATIVE_JPEG_WRITEBACK_VERIFIED')
 def test_new_wrapper_requires_complete_diagnostics(self):
  self.assertEqual(analyze(self.r)['verdict'],'NOT_VERIFIED')
 def settled_log(self,rounds=16):
  observation='GATE_OBSERVATION type=IDLE call=1 reason=PRIOR_JPEG_DONE_SETTLED address=0x00000000 before=0x00000004 after=0x00000000 read_only=1 following_stable_samples=16\n'
  good=f'GATE_RESULT type=IDLE call=1 pass=1 reason=PASS round={rounds} index=0 address=0x0 expected=0x0 actual=0x0 errno=0\n'
  summary='GATE_SUMMARY state_calls=1 idle_calls=1 rejections=0 last_type=NONE last_reason=NONE hardware_jobs=1 mmio_modified=1 safe_to_release=1\n'
  (self.r/'p21a_preflight.txt').write_text(HEADER+STATE+observation+good+summary)
 def test_settled_completion_requires_full_window(self):
  self.settled_log();r=analyze(self.r);self.assertEqual(r['verdict'],'NATIVE_JPEG_WRITEBACK_VERIFIED');self.assertTrue(r['preflight_diagnostics']['settled_status_followed_by_full_window'])
 def test_shortened_stability_window_rejected(self):
  self.settled_log(8);self.assertEqual(analyze(self.r)['verdict'],'NOT_VERIFIED')
 def test_new_completion_rise_not_treated_as_settling(self):
  self.settled_log();p=self.r/'p21a_preflight.txt';p.write_text(p.read_text().replace('before=0x00000004 after=0x00000000','before=0x00000000 after=0x00000004'))
  with self.assertRaises(ValueError):analyze(self.r)
 def test_repeated_settling_in_one_idle_call_rejected(self):
  self.settled_log();p=self.r/'p21a_preflight.txt';lines=p.read_text().splitlines();obs=next(x for x in lines if x.startswith('GATE_OBSERVATION'));p.write_text('\n'.join(lines+[obs])+'\n')
  with self.assertRaises(ValueError):analyze(self.r)
if __name__=='__main__':unittest.main(verbosity=2)
