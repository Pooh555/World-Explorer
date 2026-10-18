#ifndef WORLD_EXPLORER_TEST_INSPECTION_FIXTURE_HPP
#define WORLD_EXPLORER_TEST_INSPECTION_FIXTURE_HPP
#include "world_explorer/application/video_preflight.hpp"

#include <chrono>
#include <fstream>

#include <nlohmann/json.hpp>

namespace world_explorer::test {

struct inspection_fixture {
    std::filesystem::path root;
    VideoInspectionRequest request;

    explicit inspection_fixture(std::filesystem::path directory = {},
                                std::filesystem::path video = {},
                                const std::string& mode = "valid") {
        root =
            directory.empty()
                ? std::filesystem::temp_directory_path() /
                      ("we_inspect_" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
                : std::move(directory);
        std::filesystem::create_directories(root);
        if (video.empty()) {
            video = root / "source $ ; capture.mp4";
            std::ofstream(video) << "synthetic diagnostic, not a video";
        }
        request = { std::move(video),
                   { root / "metadata $ ; job", { root / "worker with spaces.py", WE_TEST_PYTHON }, 1 },
                   { root / "future-map", root / "future-fit", root / "future-scene" } };
        std::ofstream script(request.m_configuration.m_worker.m_script);
        script << "import sys, json, hashlib, argparse, time, threading\nfrom pathlib import Path\n"
               << "sys.path.insert(0," << nlohmann::json(WE_TEST_PROTOCOL_MODULE).dump() << ")\n"
               << "import we_protocol as p\nmode=" << nlohmann::json(mode).dump()
               << "\n"
                  "cancelled=threading.Event()\n"
                  "p.configure(sys.argv, cancel_callback=cancelled.set, "
                  "backend='synthetic_inspection',"
                  "backend_version='1', sensors=['depth'] if mode=='no_video' else ['video'],"
                  "devices=['cpu'],checkpoint_semantics='metadata "
                  "stages',checkpoint_rights='diagnostic data',"
                  "limits={'max_frames':120,'max_source_bytes':1 if mode=='small_source' else "
                  "16*1024**3,"
                  "'max_output_bytes':1 if mode=='small_output' else 32*1024**3})\n"
                  "parser=argparse.ArgumentParser()\nparser.add_argument('command',choices=['"
                  "inspect'])\n"
                  "parser.add_argument('--source',required=True)\nparser.add_argument('--job-dir',"
                  "required=True)\n"
                  "a=parser.parse_args()\nroot=Path(a.job_dir)\nsource=Path(a.source)\n"
                  "marker=root/'capabilities.json'\nprevious=marker.read_text() if marker.exists() "
                  "else ''\n"
                  "marker.write_text(previous+'START\\n')\n"
                  "p.progress('inspect','synthetic metadata inspection',.25)\n"
                  "if mode=='cancel_once' and not previous:\n"
                  " while not cancelled.wait(.01): pass\n"
                  " p.failure('CANCELLED','inspect','synthetic cooperative stop'); raise "
                  "SystemExit(130)\n"
                  "if mode=='failure':\n p.failure('BACKEND_FAILED','inspect','synthetic "
                  "failure'); raise SystemExit(2)\n"
                  "if mode=='missing':\n p.completed('inspect'); raise SystemExit(0)\n"
                  "stage=root/'stages'/('inspect-'+'c'*64)\n"
                  "if mode=='escape': stage=root.parent/'escaped'/'stages'/('inspect-'+'c'*64)\n"
                  "if not stage.exists():\n"
                  " stage.mkdir(parents=True)\n"
                  " metadata={'schema_version':1,'source':{'sha256':hashlib.sha256(source.read_"
                  "bytes()).hexdigest(),"
                  "'bytes':source.stat().st_size},'stream':{'codec_type':'video','codec_name':'"
                  "synthetic_diagnostic',"
                  "'width':2,'height':1,'time_base':'1/"
                  "30','color_transfer':'bt709'},'duration_seconds':1.,"
                  "'rotation_degrees':0,'audio':'ignored','scale_status':'ARBITRARY','color_domain'"
                  ":'srgb'}\n"
                  " if mode=='bad_metadata': metadata['stream']['time_base']='1/0'\n"
                  " result=stage/'result.json'\n result.write_text(json.dumps(metadata))\n";
        // Keep creation inside the Python condition; serialized immutable bytes
        // are reused on restart. The native fixture labels all data synthetic.
        script << "if not (stage/'completion.json').exists():\n"
                  " result=stage/'result.json'\n"
                  " completion={'schema_version':1,'status':'complete','key':'c'*64,'files':"
                  "[{'path':'result.json','bytes':result.stat().st_size,'sha256':hashlib.sha256("
                  "result.read_bytes()).hexdigest()}]}\n"
                  " (stage/'completion.json').write_text(json.dumps(completion))\n"
                  "if mode=='change_source': source.write_text('changed video')\n"
                  "if mode=='change_script': "
                  "Path(__file__).write_text(Path(__file__).read_text()+'\\n# changed\\n')\n"
                  "if mode=='wrong_stage': p.completed('poses',stage/'result.json')\n"
                  "else: p.completed('inspect',stage/'result.json')\n";
    }

    ~inspection_fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(root, ignored);
    }
};

}  // namespace world_explorer::test
#endif
