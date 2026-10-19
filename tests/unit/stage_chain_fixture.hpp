#pragma once

#include "pose_fixture.hpp"

namespace world_explorer::test {

// These workers copy tiny synthetic assets to exercise the native coordinator.
// They never solve poses or qualify capture completion/appearance.
inline void stage_worker(const std::filesystem::path& script_path,
                         const pose_fixture& fixture,
                         const std::string& mode = "valid") {
    std::ofstream script(script_path);
    script
        << "import sys,json,hashlib,shutil,threading\nfrom pathlib import Path\n"
        << "sys.path.insert(0," << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump() << ")\n"
        << "import we_protocol as p\nmode=" << nlohmann::json(mode).dump() << "\n"
        << "template=Path(" << nlohmann::json(path_utf8(fixture.root)).dump()
        << ")\n"
           "cancelled=threading.Event()\n"
           "p.configure(sys.argv,cancel_callback=cancelled.set,backend='synthetic_stage_chain',"
           "backend_version='1',"
           "sensors=['video'],devices=['cpu'],checkpoint_semantics='synthetic "
           "stages',checkpoint_rights='diagnostic data',"
           "limits={'max_frames':120,'max_source_bytes':16*1024**3,'max_output_bytes':32*1024**3})"
           "\n"
           "request=json.loads(Path(sys.argv[sys.argv.index('--request')+1]).read_text())\n"
           "root=Path(request['job_dir'])\nmarker=root/"
           "'capabilities.json'\nprevious=marker.read_text() if marker.exists() else ''\n"
           "marker.write_text(previous+'START\\n')\nkind='decode' if sys.argv[1]=='run' else "
           "'poses'\n"
           "p.progress(kind,json.dumps({'event':'stage_started','stage':kind}),1.25)\n"
           "if mode=='cancel_once' and not previous:\n"
           " while not cancelled.wait(.01): pass\n"
           " p.failure('CANCELLED',kind,'synthetic stage cancelled');raise SystemExit(130)\n"
           "if mode=='failure':\n p.failure('BACKEND_FAILED',kind,'synthetic stage failure');raise "
           "SystemExit(2)\n"
           "def inventory(directory):\n"
           " files=[{'path':str(f.relative_to(directory)).replace('\\\\','/"
           "'),'bytes':f.stat().st_size,'sha256':hashlib.sha256(f.read_bytes()).hexdigest()} for f "
           "in sorted(directory.rglob('*')) if f.is_file() and f.name!='completion.json']\n"
           " key=directory.name.split('-',1)[1]\n"
           " (directory/"
           "'completion.json').write_text(json.dumps({'schema_version':1,'status':'complete','key':"
           "key,'files':files}))\n"
           "decode=root/'stages'/('decode-'+'a'*64)\n"
           "if not decode.exists(): shutil.copytree(template/'stages'/('decode-'+'a'*64),decode)\n"
           "if kind=='decode':\n"
           " if mode=='corrupt': (decode/'images/0.png').write_text('corrupt PNG')\n"
           " p.completed('decode',decode/'result.json');raise SystemExit(0)\n"
           "journal_hash=request['journal_sha256']\n"
           "metadata=json.loads((decode/'result.json').read_text())\n"
           "metadata.update(selection='native_rgb_journal_v1',journal_sha256=journal_hash,source_"
           "sha256=json.loads(Path(request['journal']).read_text())['source_sha256'])\n"
           "(decode/'result.json').write_text(json.dumps(metadata))\ninventory(decode)\n"
           "poses=root/'stages'/('poses-'+'c'*64)\n"
           "if not poses.exists(): shutil.copytree(template/'stages'/('poses-'+'c'*64),poses)\n"
           "source=json.loads(Path(request['journal']).read_text())['source_sha256']\n"
           "(root/'decode.json').write_text(json.dumps({'key':'a'*64,'output':str(decode)}))\n"
           "(root/'poses.json').write_text(json.dumps({'key':'c'*64,'output':str(poses)}))\n"
           "(root/"
           "'result.json').write_text(json.dumps({'schema_version':1,'status':'complete','stage':'"
           "poses',"
           "'source_sha256':source,'journal_sha256':journal_hash,'output':str(poses)}))\n"
           "p.completed('poses',poses/'result.json')\n";
}

}  // namespace world_explorer::test
