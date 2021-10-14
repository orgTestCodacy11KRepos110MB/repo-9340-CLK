//
//  Amiga.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 16/07/2021.
//  Copyright © 2021 Thomas Harte. All rights reserved.
//

#include "Amiga.hpp"

#include "../../Activity/Source.hpp"
#include "../MachineTypes.hpp"

#include "../../Processors/68000/68000.hpp"

#include "../../Analyser/Static/Amiga/Target.hpp"

#include "../Utility/MemoryPacker.hpp"
#include "../Utility/MemoryFuzzer.hpp"

//#define NDEBUG
#define LOG_PREFIX "[Amiga] "
#include "../../Outputs/Log.hpp"

#include "Chipset.hpp"
#include "MemoryMap.hpp"

namespace {

// NTSC clock rate: 2*3.579545 = 7.15909Mhz.
// PAL clock rate: 7.09379Mhz; 227 cycles/line.
constexpr int PALClockRate = 7'093'790;
constexpr int NTSCClockRate = 7'159'090;

}

namespace Amiga {

class ConcreteMachine:
	public Activity::Source,
	public CPU::MC68000::BusHandler,
	public MachineTypes::MediaTarget,
	public MachineTypes::ScanProducer,
	public MachineTypes::TimedMachine,
	public Machine {
	public:
		ConcreteMachine(const Analyser::Static::Amiga::Target &target, const ROMMachine::ROMFetcher &rom_fetcher) :
			mc68000_(*this),
			chipset_(memory_, PALClockRate)
		{
			// Temporary: use a hard-coded Kickstart selection.
			constexpr ROM::Name rom_name = ROM::Name::AmigaA500Kickstart13;
			ROM::Request request(rom_name);
			auto roms = rom_fetcher(request);
			if(!request.validate(roms)) {
				throw ROMMachine::Error::MissingROMs;
			}
			Memory::PackBigEndian16(roms.find(rom_name)->second, memory_.kickstart.data());

			// For now, also hard-code assumption of PAL.
			// (Assumption is both here and in the video timing of the Chipset).
			set_clock_rate(PALClockRate);

			// Insert supplied media.
			insert_media(target.media);
		}

		// MARK: - MediaTarget.

		bool insert_media(const Analyser::Static::Media &media) final {
			return chipset_.insert(media.disks);
		}

		// MARK: - MC68000::BusHandler.
		using Microcycle = CPU::MC68000::Microcycle;
		HalfCycles perform_bus_operation(const CPU::MC68000::Microcycle &cycle, int) {

			// Do a quick advance check for Chip RAM access; add a suitable delay if required.
			HalfCycles access_delay;
			if(cycle.operation & Microcycle::NewAddress && *cycle.address < 0x20'0000) {
				access_delay = chipset_.run_until_cpu_slot().duration;
			}

			// Compute total length.
			const HalfCycles total_length = cycle.length + access_delay;

			chipset_.run_for(total_length);
			mc68000_.set_interrupt_level(chipset_.get_interrupt_level());

			// Check for assertion of reset.
			if(cycle.operation & Microcycle::Reset) {
				memory_.reset();
				LOG("Reset; PC is around " << PADHEX(8) << mc68000_.get_state().program_counter);
			}

			// Autovector interrupts.
			if(cycle.operation & Microcycle::InterruptAcknowledge) {
				mc68000_.set_is_peripheral_address(true);
				return access_delay;
			}

			// Do nothing if no address is exposed.
			if(!(cycle.operation & (Microcycle::NewAddress | Microcycle::SameAddress))) return access_delay;

			// Grab the target address to pick a memory source.
			const uint32_t address = cycle.host_endian_byte_address();

			// Set VPA if this is [going to be] a CIA access.
			mc68000_.set_is_peripheral_address((address & 0xe0'0000) == 0xa0'0000);

			if(!memory_.regions[address >> 18].read_write_mask) {
				if((cycle.operation & (Microcycle::SelectByte | Microcycle::SelectWord))) {
					// Check for various potential chip accesses.

					// Per the manual:
					//
					// CIA A is: 101x xxxx xx01 rrrr xxxx xxx0 (i.e. loaded into high byte)
					// CIA B is: 101x xxxx xx10 rrrr xxxx xxx1 (i.e. loaded into low byte)
					//
					// but in order to map 0xbfexxx to CIA A and 0xbfdxxx to CIA B, I think
					// these might be listed the wrong way around.
					//
					// Additional assumption: the relevant CIA select lines are connected
					// directly to the chip enables.
					if((address & 0xe0'0000) == 0xa0'0000) {
						const int reg = address >> 8;

						if(cycle.operation & Microcycle::Read) {
							uint16_t result = 0xffff;
							if(!(address & 0x1000)) result &= 0xff00 | (chipset_.cia_a.read(reg) << 0);
							if(!(address & 0x2000)) result &= 0x00ff | (chipset_.cia_b.read(reg) << 8);
							cycle.set_value16(result);
						} else {
							if(!(address & 0x1000)) chipset_.cia_a.write(reg, cycle.value8_low());
							if(!(address & 0x2000)) chipset_.cia_b.write(reg, cycle.value8_high());
						}

//						LOG("CIA " << (((address >> 12) & 3)^3) << " " << (cycle.operation & Microcycle::Read ? "read " : "write ") << std::dec << (reg & 0xf) << " of " << PADHEX(2) << +cycle.value8_low());
					} else if(address >= 0xdf'f000 && address <= 0xdf'f1be) {
						chipset_.perform(cycle);
					} else {
						// This'll do for open bus, for now.
						if(cycle.operation & Microcycle::Read) {
							cycle.set_value16(0xffff);
						}

						// Don't log for the region that is definitely just ROM this machine doesn't have.
						if(address < 0xf0'0000) {
							LOG("Unmapped " << (cycle.operation & Microcycle::Read ? "read from " : "write to ") << PADHEX(6) << ((*cycle.address)&0xffffff) << " of " << cycle.value16());
						}
					}
				}
			} else {
				// A regular memory access.
				cycle.apply(
					&memory_.regions[address >> 18].contents[address],
					memory_.regions[address >> 18].read_write_mask
				);
			}

			return access_delay;
		}

	private:
		CPU::MC68000::Processor<ConcreteMachine, true> mc68000_;

		// MARK: - Memory map.

		MemoryMap memory_;

		// MARK: - Chipset.

		Chipset chipset_;
		
		// MARK: - Activity Source
		void set_activity_observer(Activity::Observer *observer) final {
			chipset_.set_activity_observer(observer);
		}

		// MARK: - MachineTypes::ScanProducer.

		void set_scan_target(Outputs::Display::ScanTarget *scan_target) final {
			chipset_.set_scan_target(scan_target);
		}

		Outputs::Display::ScanStatus get_scaled_scan_status() const {
			return chipset_.get_scaled_scan_status();
		}

		// MARK: - MachineTypes::TimedMachine.

		void run_for(const Cycles cycles) {
			mc68000_.run_for(cycles);
		}
};

}


using namespace Amiga;

Machine *Machine::Amiga(const Analyser::Static::Target *target, const ROMMachine::ROMFetcher &rom_fetcher) {
	using Target = Analyser::Static::Amiga::Target;
	const Target *const amiga_target = dynamic_cast<const Target *>(target);
	return new Amiga::ConcreteMachine(*amiga_target, rom_fetcher);
}

Machine::~Machine() {}
