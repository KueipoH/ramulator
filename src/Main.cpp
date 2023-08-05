#include "Processor.h"
#include "Config.h"
#include "Controller.h"
#include "SpeedyController.h"
#include "Memory.h"
#include "DRAM.h"
#include "Statistics.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdlib.h>
#include <functional>
#include <map>

/* Standards */
#include "Gem5Wrapper.h"
#include "DDR3.h"
#include "DDR4.h"
#include "DSARP.h"
#include "GDDR5.h"
#include "LPDDR3.h"
#include "LPDDR4.h"
#include "WideIO.h"
#include "WideIO2.h"
#include "HBM.h"
#include "SALP.h"
#include "ALDRAM.h"
#include "TLDRAM.h"
#include "STTMRAM.h"
#include "PCM.h"

using namespace std;
using namespace ramulator;

bool ramulator::warmup_complete = false;

class DRAMFactory {
public:
    template <typename T>
    static T* createDRAM(Config& configs) {
        return new T(configs["org"], configs["speed"]);
    }
};

template<typename T>
void run_dramtrace(const Config& configs, Memory<T, Controller>& memory, const char* tracename) {

    /* initialize DRAM trace */
    Trace trace(tracename);

    /* run simulation */
    bool stall = false, end = false;
    int reads = 0, writes = 0, clks = 0;
    long addr = 0;
    Request::Type type = Request::Type::READ;
    map<int, int> latencies;
    auto read_complete = [&latencies](Request& r){latencies[r.depart - r.arrive]++;};

    Request req(addr, type, read_complete);

    while (!end || memory.pending_requests()){
        if (!end && !stall){
            end = !trace.get_dramtrace_request(addr, type);
        }

        if (!end){
            req.addr = addr;
            req.type = type;
            stall = !memory.send(req);
            if (!stall){
                if (type == Request::Type::READ) reads++;
                else if (type == Request::Type::WRITE) writes++;
            }
        }
        else {
            memory.set_high_writeq_watermark(0.0f); // make sure that all write requests in the
                                                    // write queue are drained
        }

        memory.tick();
        clks ++;
        Stats::curTick++; // memory clock, global, for Statistics
    }
    // This a workaround for statistics set only initially lost in the end
    memory.finish();
    Stats::statlist.printall();

}

template <typename T>
void run_cputrace(const Config& configs, Memory<T, Controller>& memory, const std::vector<const char *>& files)
{
    int cpu_tick = configs.get_cpu_tick();
    int mem_tick = configs.get_mem_tick();
    auto send = bind(&Memory<T, Controller>::send, &memory, placeholders::_1);
    Processor proc(configs, files, send, memory);

    long warmup_insts = configs.get_warmup_insts();
    bool is_warming_up = (warmup_insts != 0);

    for(long i = 0; is_warming_up; i++){
        proc.tick();
        Stats::curTick++;
        if (i % cpu_tick == (cpu_tick - 1))
            for (int j = 0; j < mem_tick; j++)
                memory.tick();

        is_warming_up = false;
        for(int c = 0; c < proc.cores.size(); c++){
            if(proc.cores[c]->get_insts() < warmup_insts)
                is_warming_up = true;
        }

        if (is_warming_up && proc.has_reached_limit()) {
            printf("WARNING: The end of the input trace file was reached during warmup. "
                    "Consider changing warmup_insts in the config file. \n");
            break;
        }

    }

    warmup_complete = true;
    printf("Warmup complete! Resetting stats...\n");
    Stats::reset_stats();
    proc.reset_stats();
    assert(proc.get_insts() == 0);

    printf("Starting the simulation...\n");

    int tick_mult = cpu_tick * mem_tick;
    for (long i = 0; ; i++) {
        if (((i % tick_mult) % mem_tick) == 0) { // When the CPU is ticked cpu_tick times,
                                                 // the memory controller should be ticked mem_tick times
            proc.tick();
            Stats::curTick++; // processor clock, global, for Statistics

            if (configs.calc_weighted_speedup()) {
                if (proc.has_reached_limit()) {
                    break;
                }
            } else {
                if (configs.is_early_exit()) {
                    if (proc.finished())
                    break;
                } else {
                if (proc.finished() && (memory.pending_requests() == 0))
                    break;
                }
            }
        }

        if (((i % tick_mult) % cpu_tick) == 0) // TODO_hasan: Better if the processor ticks the memory controller
            memory.tick();

    }
    // This a workaround for statistics set only initially lost in the end
    memory.finish();
    Stats::statlist.printall();
}

template<typename T>
void start_run(const Config& configs, T* spec, const vector<const char*>& files) {
  // initiate controller and memory
  int C = configs.get_channels(), R = configs.get_ranks();
  // Check and Set channel, rank number
  spec->set_channel_number(C);
  spec->set_rank_number(R);
  std::vector<Controller<T>*> ctrls;
  for (int c = 0 ; c < C ; c++) {
    DRAM<T>* channel = new DRAM<T>(spec, T::Level::Channel);
    channel->id = c;
    channel->regStats("");
    Controller<T>* ctrl = new Controller<T>(configs, channel);
    ctrls.push_back(ctrl);
  }
  Memory<T, Controller> memory(configs, ctrls);

  assert(files.size() != 0);
  if (configs["trace_type"] == "CPU") {
    run_cputrace(configs, memory, files);
  } else if (configs["trace_type"] == "DRAM") {
    run_dramtrace(configs, memory, files[0]);
  }
}

int main(int argc, const char *argv[])
{
    if (argc < 2) {
        printf("Usage: %s <configs-file> --mode=cpu,dram [--stats <filename>] <trace-filename1> <trace-filename2>\n"
            "Example: %s ramulator-configs.cfg --mode=cpu cpu.trace cpu.trace\n", argv[0], argv[0]);
        return 0;
    }

    Config configs(argv[1]);

    const std::string& standard = configs["standard"];
    assert(standard != "" || "DRAM standard should be specified.");

    const char *trace_type = strstr(argv[2], "=");
    trace_type++;
    if (strcmp(trace_type, "cpu") == 0) {
      configs.add("trace_type", "CPU");
    } else if (strcmp(trace_type, "dram") == 0) {
      configs.add("trace_type", "DRAM");
    } else {
      printf("invalid trace type: %s\n", trace_type);
      assert(false);
    }

    int trace_start = 3;
    string stats_out;
    if (strcmp(argv[trace_start], "--stats") == 0) {
      Stats::statlist.output(argv[trace_start+1]);
      stats_out = argv[trace_start+1];
      trace_start += 2;
    } else {
      Stats::statlist.output(standard+".stats");
      stats_out = standard + string(".stats");
    }

    // A separate file defines mapping for easy config.
    if (strcmp(argv[trace_start], "--mapping") == 0) {
      configs.add("mapping", argv[trace_start+1]);
      trace_start += 2;
    } else {
      configs.add("mapping", "defaultmapping");
    }

    std::vector<const char*> files(&argv[trace_start], &argv[argc]);
    configs.set_core_num(argc - trace_start);

    DRAMBase* dram;
    if (standard == "DDR3") {
        dram = DRAMFactory::createDRAM<DDR3>(configs);
    } else if (standard == "DDR4") {
        dram = DRAMFactory::createDRAM<DDR4>(configs);
    } else if (standard == "SALP-MASA") {
        dram = DRAMFactory::createDRAM<SALP>(configs);
    } else if (standard == "LPDDR3") {
        dram = DRAMFactory::createDRAM<LPDDR3>(configs);
    } else if (standard == "LPDDR4") {
        // total cap: 2GB, 1/2 of others
        dram = DRAMFactory::createDRAM<LPDDR4>(configs);
    } else if (standard == "GDDR5") {
        dram = DRAMFactory::createDRAM<GDDR5>(configs);
    } else if (standard == "HBM") {
        dram = DRAMFactory::createDRAM<HBM>(configs);
    } else if (standard == "WideIO") {
        // total cap: 1GB, 1/4 of others
        dram = DRAMFactory::createDRAM<WideIO>(configs);
    } else if (standard == "WideIO2") {
        // total cap: 2GB, 1/2 of others
        dram = DRAMFactory::createDRAM<WideIO2>(configs);
    } else if (standard == "STTMRAM") {
        dram = DRAMFactory::createDRAM<STTMRAM>(configs);
    } else if (standard == "PCM") {
        dram = DRAMFactory::createDRAM<PCM>(configs);
    // Various refresh mechanisms
    } else if (standard == "DSARP") {
        dram = DRAMFactory::createDRAM<DSARP>(configs);
    } else if (standard == "ALDRAM") {
        dram = DRAMFactory::createDRAM<ALDRAM>(configs);
    } else if (standard == "TLDRAM") {
        dram = DRAMFactory::createDRAM<TLDRAM>(configs);
    }
    start_run(configs, dram, files);

    printf("Simulation done. Statistics written to %s\n", stats_out.c_str());

    return 0;
}
