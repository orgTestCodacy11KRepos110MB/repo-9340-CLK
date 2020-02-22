//
//  PatrikRakTests.swift
//  Clock Signal
//
//  Created by Thomas Harte on 22/02/2020.
//  Copyright 2017 Thomas Harte. All rights reserved.
//

import XCTest
import Foundation

class PatrikRakTests: XCTestCase, CSTestMachineTrapHandler {

	fileprivate var done = false
	fileprivate var output = ""

	private func runTest(_ name: String) {
		if let filename = Bundle(for: type(of: self)).path(forResource: name, ofType: "tap") {
			if let testData = try? Data(contentsOf: URL(fileURLWithPath: filename)) {

				// Do a minor parsing of the TAP file to find the final file.
				var dataPointer = 0
				while dataPointer < testData.count {
					let blockSize = Int(testData[dataPointer]) + Int(testData[dataPointer+1]) << 8

					dataPointer += 2 + blockSize
					break
				}

				// Create a machine.
				let machine = CSTestMachineZ80()
				machine.setData(testData, atAddress: 0x0100)

				// Add a RET and a trap at 10h, this is the Spectrum's system call for outputting text.
				machine.setValue(0xc9, atAddress: 0x0010)
				machine.addTrapAddress(0x0010);
				machine.trapHandler = self

				// Add a call to $8000 and then an infinite loop; these tests load at $8000 and RET when done.
				machine.setValue(0xcd, atAddress: 0x7000)
				machine.setValue(0x00, atAddress: 0x7001)
				machine.setValue(0x80, atAddress: 0x7002)
				machine.setValue(0xc3, atAddress: 0x7003)
				machine.setValue(0x03, atAddress: 0x7004)
				machine.setValue(0x70, atAddress: 0x7005)

				// seed execution at 0x7000
				machine.setValue(0x7000, for: .programCounter)

				// run!
				let cyclesPerIteration: Int32 = 400_000_000
				var cyclesToDate: TimeInterval = 0
				let startDate = Date()
				var printDate = Date()
				let printMhz = false
				while !done {
					machine.runForNumber(ofCycles: cyclesPerIteration)
					cyclesToDate += TimeInterval(cyclesPerIteration)
					if printMhz && printDate.timeIntervalSinceNow < -5.0 {
						print("\(cyclesToDate / -startDate.timeIntervalSinceNow) Mhz")
						printDate = Date()
					}
				}

				let targetOutput =
					"Z80doc instruction exerciser\n\r"			+
					"<adc,sbc> hl,<bc,de,hl,sp>....  OK\n\r"	+
					"add hl,<bc,de,hl,sp>..........  OK\n\r"	+
					"add ix,<bc,de,ix,sp>..........  OK\n\r"	+
					"add iy,<bc,de,iy,sp>..........  OK\n\r"	+
					"aluop a,nn....................  OK\n\r"	+
					"aluop a,<b,c,d,e,h,l,(hl),a>..  OK\n\r"	+
					"aluop a,<ixh,ixl,iyh,iyl>.....  OK\n\r"	+
					"aluop a,(<ix,iy>+1)...........  OK\n\r"	+
					"bit n,(<ix,iy>+1).............  OK\n\r"	+
					"bit n,<b,c,d,e,h,l,(hl),a>....  OK\n\r"	+
					"cpd<r>........................  OK\n\r"	+
					"cpi<r>........................  OK\n\r"	+
					"<daa,cpl,scf,ccf>.............  OK\n\r"	+
					"<inc,dec> a...................  OK\n\r"	+
					"<inc,dec> b...................  OK\n\r"	+
					"<inc,dec> bc..................  OK\n\r"	+
					"<inc,dec> c...................  OK\n\r"	+
					"<inc,dec> d...................  OK\n\r"	+
					"<inc,dec> de..................  OK\n\r"	+
					"<inc,dec> e...................  OK\n\r"	+
					"<inc,dec> h...................  OK\n\r"	+
					"<inc,dec> hl..................  OK\n\r"	+
					"<inc,dec> ix..................  OK\n\r"	+
					"<inc,dec> iy..................  OK\n\r"	+
					"<inc,dec> l...................  OK\n\r"	+
					"<inc,dec> (hl)................  OK\n\r"	+
					"<inc,dec> sp..................  OK\n\r"	+
					"<inc,dec> (<ix,iy>+1).........  OK\n\r"	+
					"<inc,dec> ixh.................  OK\n\r"	+
					"<inc,dec> ixl.................  OK\n\r"	+
					"<inc,dec> iyh.................  OK\n\r"	+
					"<inc,dec> iyl.................  OK\n\r"	+
					"ld <bc,de>,(nnnn).............  OK\n\r"	+
					"ld hl,(nnnn)..................  OK\n\r"	+
					"ld sp,(nnnn)..................  OK\n\r"	+
					"ld <ix,iy>,(nnnn).............  OK\n\r"	+
					"ld (nnnn),<bc,de>.............  OK\n\r"	+
					"ld (nnnn),hl..................  OK\n\r"	+
					"ld (nnnn),sp..................  OK\n\r"	+
					"ld (nnnn),<ix,iy>.............  OK\n\r"	+
					"ld <bc,de,hl,sp>,nnnn.........  OK\n\r"	+
					"ld <ix,iy>,nnnn...............  OK\n\r"	+
					"ld a,<(bc),(de)>..............  OK\n\r"	+
					"ld <b,c,d,e,h,l,(hl),a>,nn....  OK\n\r"	+
					"ld (<ix,iy>+1),nn.............  OK\n\r"	+
					"ld <b,c,d,e>,(<ix,iy>+1)......  OK\n\r"	+
					"ld <h,l>,(<ix,iy>+1)..........  OK\n\r"	+
					"ld a,(<ix,iy>+1)..............  OK\n\r"	+
					"ld <ixh,ixl,iyh,iyl>,nn.......  OK\n\r"	+
					"ld <bcdehla>,<bcdehla>........  OK\n\r"	+
					"ld <bcdexya>,<bcdexya>........  OK\n\r"	+
					"ld a,(nnnn) / ld (nnnn),a.....  OK\n\r"	+
					"ldd<r> (1)....................  OK\n\r"	+
					"ldd<r> (2)....................  OK\n\r"	+
					"ldi<r> (1)....................  OK\n\r"	+
					"ldi<r> (2)....................  OK\n\r"	+
					"neg...........................  OK\n\r"	+
					"<rrd,rld>.....................  OK\n\r"	+
					"<rlca,rrca,rla,rra>...........  OK\n\r"	+
					"shf/rot (<ix,iy>+1)...........  OK\n\r"	+
					"shf/rot <b,c,d,e,h,l,(hl),a>..  OK\n\r"	+
					"<set,res> n,<bcdehl(hl)a>.....  OK\n\r"	+
					"<set,res> n,(<ix,iy>+1).......  OK\n\r"	+
					"ld (<ix,iy>+1),<b,c,d,e>......  OK\n\r"	+
					"ld (<ix,iy>+1),<h,l>..........  OK\n\r"	+
					"ld (<ix,iy>+1),a..............  OK\n\r"	+
					"ld (<bc,de>),a................  OK\n\r"	+
					"Tests complete\n\r"
				XCTAssertEqual(targetOutput, output);
			}
		}
	}

	func testCCF() {
		runTest("z80ccf")
	}

	func testMachine(_ testMachine: CSTestMachine, didTrapAtAddress address: UInt16) {
		let testMachineZ80 = testMachine as! CSTestMachineZ80
		switch address {
			case 0x0010:
				print("TODO")

				let cRegister = testMachineZ80.value(for: .C)
				var textToAppend = ""
				switch cRegister {
					case 9:
						var address = testMachineZ80.value(for: .DE)
						var character: Character = " "
						while true {
							character = Character(UnicodeScalar(testMachineZ80.value(atAddress: address)))
							if character == "$" {
								break
							}
							textToAppend += String(character)
							address = address + 1
						}
					case 5:
						textToAppend = String(describing: UnicodeScalar(testMachineZ80.value(for: .E)))
					case 0:
						done = true
					default:
						break
				}
				output += textToAppend
				print(textToAppend)

			case 0x7003:
				done = true

			default:
				break
		}
	}
}
