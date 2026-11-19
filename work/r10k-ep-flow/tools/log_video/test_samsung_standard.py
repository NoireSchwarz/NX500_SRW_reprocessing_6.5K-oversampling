"""Math/range/order regressions, with independent official-LUT comparison."""
import os
from pathlib import Path
import tempfile
import unittest
import numpy as np
from samsung_standard import (encode, decode, quantize, normalize, convert_samples,
    primary_matrix, encode_linear_rgb, read_cube, sample_cube, Cube, verify_official_files)


class StandardTests(unittest.TestCase):
    def test_published_anchors_in_both_bit_depths(self):
        scene = [0, .01, .18, .9, 12]
        np.testing.assert_array_equal(quantize(encode(scene), bits=10, sample_range='full'), [128,211,540,725,1023])
        np.testing.assert_array_equal(quantize(encode(scene), bits=8, sample_range='full'), [32,53,135,181,255])
        np.testing.assert_allclose(encode(scene), [.125124,.206562,.527859,.708700,1.], atol=5e-7, rtol=0)

    def test_published_seams_are_preserved(self):
        self.assertEqual(float(encode(-.051)), 0.)
        # Rounded publication constants have a small positive seam at x0.
        self.assertAlmostEqual(float(encode(-.05)), 3.824615476433202e-6, places=15)
        self.assertEqual(float(decode(-.001)), -.05)
        self.assertLess(abs(float(encode(.01))-.206561909), 1e-9)
        x = np.r_[np.linspace(-.0499,.00999,3000), np.geomspace(.0101,12,3000)]
        np.testing.assert_allclose(decode(encode(x)), x, rtol=0, atol=1e-12)

    def test_range_endpoints_and_footroom(self):
        for bits, expected in [(8,[16,235]),(10,[64,940])]:
            np.testing.assert_array_equal(quantize([0,1],bits=bits,sample_range='legal'), expected)
            np.testing.assert_array_equal(normalize(expected,bits=bits,sample_range='legal'), [0,1])
        self.assertLess(float(normalize(0,bits=8,sample_range='legal')),0)
        self.assertGreater(float(normalize(255,bits=8,sample_range='legal')),1)
        np.testing.assert_array_equal(convert_samples([0,128,540,725,1023], source_bits=10, source_range='full',target_bits=8,target_range='full'), [0,32,135,181,255])
        np.testing.assert_array_equal(convert_samples([16,235],source_bits=8,source_range='legal',target_bits=10,target_range='legal'), [64,940])

    def test_eight_bit_expansion_does_not_add_precision(self):
        expanded=convert_samples(np.arange(256), source_bits=8,source_range='full',target_bits=10,target_range='full')
        self.assertEqual(len(np.unique(expanded)),256)
        down=convert_samples(np.arange(1024),source_bits=10,source_range='full',target_bits=8,target_range='full')
        back=convert_samples(down,source_bits=8,source_range='full',target_bits=10,target_range='full')
        self.assertLessEqual(int(np.max(abs(back.astype(int)-np.arange(1024)))),2)

    def test_invalid_inputs_fail(self):
        for bad in [float('nan'),float('inf')]:
            with self.assertRaises(ValueError): encode(bad)
            with self.assertRaises(ValueError): quantize(bad,bits=8,sample_range='full')
        for args in [(12,'full'),(8,'unknown')]:
            with self.assertRaises(ValueError): quantize(.5,bits=args[0],sample_range=args[1])
        for bad in [-1,256,.5]:
            with self.assertRaises(ValueError): normalize(bad,bits=8,sample_range='full')
        with self.assertRaises(ValueError): encode_linear_rgb([1,2],source_primaries='BT709')
        with self.assertRaises(KeyError): primary_matrix('UNKNOWN','BT2020')

    def test_primary_matrix_independent_reference(self):
        expected=np.array([[.6274038959,.3292830384,.0433130657],
                           [.0690972894,.9195403951,.0113623156],
                           [.0163914389,.0880133079,.8955952532]])
        m=primary_matrix('BT709','BT2020')
        np.testing.assert_allclose(m,expected,atol=6e-11,rtol=0)
        np.testing.assert_allclose(m@np.ones(3),np.ones(3),atol=1e-15,rtol=0)
        np.testing.assert_allclose(primary_matrix('BT2020','BT709')@m,np.eye(3),atol=1e-15,rtol=0)
        np.testing.assert_allclose(encode_linear_rgb([[.18,.18,.18]],source_primaries='BT709'),encode([[.18,.18,.18]]),atol=1e-15,rtol=0)

    def test_color_conversion_precedes_log(self):
        m=primary_matrix('BT709','BT2020'); rgb=np.array([1.,0.,0.])
        correct=encode_linear_rgb(rgb,source_primaries='BT709')
        wrong=encode(rgb)@m.T
        self.assertGreater(float(np.max(abs(correct-wrong))*1023),250)
        # Grey alone would miss the incorrect order.
        np.testing.assert_allclose(encode([.18]*3)@m.T,encode_linear_rgb([.18]*3,source_primaries='BT709'),atol=1e-15,rtol=0)

    def test_cube_axes_and_domain_are_not_swapped(self):
        nodes=np.array([[g,b,r] for b in [0.,1.] for g in [0.,1.] for r in [0.,1.]])
        c=Cube(2,3,np.array([0,0,0]),np.array([2,3,4]),nodes)
        np.testing.assert_allclose(sample_cube(c,[.4,.9,2.8]),[.3,.7,.2],rtol=0,atol=1e-15)
        np.testing.assert_allclose(sample_cube(c,[[0,0,0],[2,3,4]]),[[0,0,0],[1,1,1]],rtol=0,atol=0)
        one=Cube(2,1,np.zeros(3),np.ones(3),np.array([[0,1,2],[2,4,6]]))
        np.testing.assert_allclose(sample_cube(one,[.2,.3,.4]),[.4,1.9,3.6],rtol=0,atol=1e-15)

    def test_truncated_and_nonfinite_cube_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            p=Path(temp)/'bad.cube'
            for text in ['LUT_3D_SIZE 2\n0 0 0\n','LUT_1D_SIZE 2\n0 0 0\nnan 1 1\n',
                         'LUT_1D_SIZE 2\nLUT_3D_SIZE 2\n0 0 0\n1 1 1\n']:
                p.write_text(text,encoding='ascii')
                with self.assertRaises(ValueError): read_cube(p)

    @unittest.skipUnless(os.environ.get('NX500_SAMSUNG_OFFICIAL'), 'set official directory for independent comparison')
    def test_official_decoder_and_3d_lut(self):
        root=Path(os.environ['NX500_SAMSUNG_OFFICIAL'])
        self.assertEqual(len(verify_official_files(root)),4)
        c=read_cube(root/'Samsung+Log+to+Linear+1DLUT_v1.0.cube')
        self.assertEqual((c.size,c.dimensions),(4096,1))
        np.testing.assert_array_equal(c.data[:,0],c.data[:,1])
        np.testing.assert_array_equal(c.data[:,1],c.data[:,2])
        err=abs(decode(np.linspace(0,1,4096))-c.data[:,0])
        self.assertLess(float(err.max()),1.3e-5)
        rgb=read_cube(root/'Samsung+Log+to+Rec709+3DLUT_v1.0.cube')
        self.assertEqual((rgb.size,rgb.dimensions),(65,3))
        self.assertTrue(np.all((rgb.data>=0)&(rgb.data<=1)))
        # Exact knots must match the downloaded CUBE, including endpoints.
        rng=np.random.default_rng(20261117);idx=rng.integers(0,65,(100,3))
        want=rgb.data[(idx[:,2]*65+idx[:,1])*65+idx[:,0]]
        np.testing.assert_allclose(sample_cube(rgb,idx/64),want,atol=1e-15,rtol=0)


if __name__=='__main__':
    unittest.main()
