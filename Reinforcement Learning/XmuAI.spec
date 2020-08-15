# -*- mode: python ; coding: utf-8 -*-
import os
import importlib
block_cipher = None


a = Analysis(['XmuAI.py'],
             pathex=['C:\\Work Space\\CurlingAICode\\CurlingAIPython'],
             binaries=[],
             datas=[('model','model'),(os.path.join(os.path.dirname(importlib.import_module('tensorflow').__file__),
                                  "lite/experimental/microfrontend/python/ops/_audio_microfrontend_op.so"),
                     "tensorflow/lite/experimental/microfrontend/python/ops/"),
                     (os.path.join(os.path.dirname(importlib.import_module('sklearn').__file__), 
                     ".libs/vcomp140.dll"), "sklearn/.libs/"),('memory','memory')],
             hiddenimports=['pkg_resources.py2_warn'],
             hookspath=[],
             runtime_hooks=[],
             excludes=[],
             win_no_prefer_redirects=False,
             win_private_assemblies=False,
             cipher=block_cipher,
             noarchive=False)
pyz = PYZ(a.pure, a.zipped_data,
             cipher=block_cipher)
exe = EXE(pyz,
          a.scripts,
          [],
          exclude_binaries=True,
          name='XmuAI',
          debug=False,
          bootloader_ignore_signals=False,
          strip=False,
          upx=True,
          console=True )
coll = COLLECT(exe,
               a.binaries,
               a.zipfiles,
               a.datas,
               strip=False,
               upx=True,
               upx_exclude=[],
               name='XmuAI')
