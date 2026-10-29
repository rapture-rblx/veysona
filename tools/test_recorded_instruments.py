#!/usr/bin/env python3
"""Offline unit tests for recorded-sample installer mappings and safeguards."""
import io
from pathlib import Path
import sys
import unittest
import zipfile
sys.path.insert(0,str(Path(__file__).parent))
import install_recorded_instruments as inst

class RecordedInstrumentTests(unittest.TestCase):
 def zip_of(self,paths):
  blob=io.BytesIO()
  with zipfile.ZipFile(blob,'w') as zf:
   for name in paths:zf.writestr('pack/ogg/'+name,b'audio-placeholder')
  blob.seek(0)
  return zipfile.ZipFile(blob)
 def test_piano_velocity_and_midi_name(self):
  zf=self.zip_of(['INSPiano_Vel01_C4_01.ogg','INSPiano_Vel03_C4_01.ogg'])
  self.assertEqual(inst.notes_from_zip(zf,'piano',[60])[60],'pack/ogg/INSPiano_Vel03_C4_01.ogg')
 def test_nylon_string_and_velocity(self):
  zf=self.zip_of(['INSGuitarNylon_StringE_Vel02Pick_E2_01.ogg'])
  self.assertTrue(inst.notes_from_zip(zf,'nylon',[40])[40].endswith('E2_01.ogg'))
 def test_velocity_layer_selects_distinct_recording(self):
  zf=self.zip_of(['INSPiano_Vel01_C4_01.ogg','INSPiano_Vel03_C4_01.ogg'])
  self.assertTrue(inst.notes_from_zip(zf,'piano',[60],layer=1)[60].endswith('Vel01_C4_01.ogg'))
  self.assertTrue(inst.notes_from_zip(zf,'piano',[60],layer=3)[60].endswith('Vel03_C4_01.ogg'))
  with self.assertRaises(RuntimeError):inst.notes_from_zip(zf,'piano',[60],layer=4)
 def test_reject_missing_recording(self):
  with self.assertRaisesRegex(RuntimeError,'no steel recording'):
   inst.notes_from_zip(self.zip_of([]),'steel',[60])
 def test_license_and_checksums_pinned(self):
  self.assertEqual(len(inst.DRUM_KITS),3)
  for kit_name,drums in inst.DRUM_KITS.items():
   self.assertEqual(len(drums),8,kit_name)
   self.assertEqual(len(set(n for n,_,_,_ in drums)),8)
   self.assertTrue(all(filename.endswith('.flac') for _,_,filename,_ in drums))
  self.assertEqual(len(set(tuple(d[2] for d in rows) for rows in inst.DRUM_KITS.values())),3)
  for spec in inst.META.values():
   for _,digest in spec['files'].values():
    self.assertEqual(len(digest),64)
    self.assertTrue(all(c in '0123456789abcdef' for c in digest))
 def test_note_names(self):
  self.assertEqual(inst.note_name(60),'C4')
  self.assertEqual(inst.note_name(40),'E2')

if __name__=='__main__':unittest.main()
