//
//  1770.cpp
//  Clock Signal
//
//  Created by Thomas Harte on 17/09/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

#include "1770.hpp"

using namespace WD;

WD1770::WD1770() :
	Storage::Disk::Drive(1000000, 8, 300),
	state_(State::Waiting), status_(0), has_command_(false) {
	set_is_double_density(false);
}

void WD1770::set_is_double_density(bool is_double_density)
{
	Storage::Time bit_length;
	bit_length.length = 1;
	bit_length.clock_rate = is_double_density ? 500000 : 250000;
	set_expected_bit_length(bit_length);
}

void WD1770::set_register(int address, uint8_t value)
{
	switch(address&3)
	{
		case 0:
			command_ = value;
			has_command_ = true;
			// TODO: is this force interrupt?
		break;
		case 1:		track_ = value;		break;
		case 2:		sector_ = value;	break;
		case 3:		data_ = value;		break;
	}
}

uint8_t WD1770::get_register(int address)
{
	switch(address&3)
	{
		default:	return status_;
		case 1:		return track_;
		case 2:		return sector_;
		case 3:		return data_;
	}
}

void WD1770::run_for_cycles(unsigned int number_of_cycles)
{
	// perform one step every eight cycles, arbitrariy as I can find no timing documentation
	cycles += number_of_cycles;
	while(cycles > 8)
	{
		cycles -= 8;
		if(status_ & Flag::MotorOn) Storage::Disk::Drive::run_for_cycles(1);

		switch(state_)
		{
			case State::Waiting:
				if(has_command_)
				{
					has_command_ = false;
					if(command_ & 0x80)
						state_ = (command_ & 0x40) ? State::BeginType3 : State::BeginType2;
					else
						state_ = State::BeginType1;
				}
			continue;

			case State::WaitForSixIndexPulses:
				status_ |= Flag::MotorOn;
				// deliberately empty; logic is in ::process_index_hole
			continue;

#pragma mark - Type 1

			case State::BeginType1:
				status_ |= Flag::Busy;
				status_ &= ~(Flag::DataRequest | Flag::CRCError);
				set_interrupt_request(false);
				state_ = State::BeginType1PostSpin;
				if(command_ & 0x08)
				{
					wait_six_index_pulses_.next_state = state_;
					index_hole_count_ = 0;
					state_ = State::WaitForSixIndexPulses;
				}
			continue;

			case State::BeginType1PostSpin:
				switch(command_ >> 4)
				{
					case 0:	// restore
						track_ = 0xff;	// deliberate fallthrough
					case 1:	// seek
						data_ = 0x00;
					break;
					case 2: case 3: // step
					break;
					case 4: case 5: // step in
						is_step_in_ = true;
					break;
					case 6: case 7: // step out
						is_step_in_ = false;
					break;
				}

				if(!(command_ >> 5))
					state_ = State::TestTrack;
				else
					state_ = (command_ & 0x10) ? State::TestDirection : State::TestHead;
			continue;

			case State::TestTrack:
				data_shift_register_ = data_;
				if(track_ == data_shift_register_)
					state_ = State::TestVerify;
				else
				{
					is_step_in_ = (data_shift_register_ < track_);
					state_ = State::TestDirection;
				}
			continue;

			case State::TestDirection:
				track_ += is_step_in_ ? -1 : +1;
				state_ = State::TestHead;
			continue;

			case State::TestHead:
				if(get_is_track_zero() && !is_step_in_)
				{
					track_ = 0;
					state_ = State::TestVerify;
				}
				else
				{
					step(is_step_in_ ? 1 : -1);
					state_ = State::StepDelay;
					step_delay_.count = 0;
				}
			continue;

			case State::StepDelay:
				if(step_delay_.count == (command_&3))
				{
					state_ = (command_ >> 5) ? State::TestVerify : State::TestTrack;
				}
				step_delay_.count++;
			continue;

			case State::TestVerify:
				if(command_ & 0x04)
				{
					state_ = State::VerifyTrack;
				}
				else
				{
					set_interrupt_request(true);
					status_ &= ~Flag::Busy;
					state_ = State::Waiting;
				}
			continue;

#pragma mark - Type 2

			case State::BeginType2:
				status_ |= Flag::Busy;
				status_ &= ~(Flag::DataRequest | Flag::LostData | Flag::RecordNotFound | 0x60);
				state_ = State::TestPause;
				if(!(command_&0x08))
				{
					wait_six_index_pulses_.next_state = state_;
					index_hole_count_ = 0;
					state_ = State::WaitForSixIndexPulses;
				}
			continue;

			case State::TestPause:
				// TODO: pause for 30ms if E is set
				state_ = State::TestWrite;
			continue;

//			case State::TestWrite:
//			continue;

//     +------+----------+-------------------------+
//     !	    !	       !	   BITS 	 !
//     ! TYPE ! COMMAND  !  7  6	5  4  3  2  1  0 !
//     +------+----------+-------------------------+
//     !	 1  ! Restore  !  0  0	0  0  h  v r1 r0 !
//     !	 1  ! Seek     !  0  0	0  1  h  v r1 r0 !
//     !	 1  ! Step     !  0  0	1  u  h  v r1 r0 !
//     !	 1  ! Step-in  !  0  1	0  u  h  v r1 r0 !
//     !	 1  ! Step-out !  0  1	1  u  h  v r1 r0 !
//     !	 2  ! Rd sectr !  1  0	0  m  h  E  0  0 !
//     !	 2  ! Wt sectr !  1  0	1  m  h  E  P a0 !
//     !	 3  ! Rd addr  !  1  1	0  0  h  E  0  0 !
//     !	 3  ! Rd track !  1  1	1  0  h  E  0  0 !
//     !	 3  ! Wt track !  1  1	1  1  h  E  P  0 !
//     !	 4  ! Forc int !  1  1	0  1 i3 i2 i1 i0 !
//     +------+----------+-------------------------+

			default:
			{
				static bool has_hit_error = false;
				if(!has_hit_error)
					printf("Unhandled state %d!\n", state_);
				has_hit_error = true;
			}
			return;
		}
	}
}

void WD1770::process_input_bit(int value, unsigned int cycles_since_index_hole)
{
}

void WD1770::process_index_hole()
{
	index_hole_count_++;

	if(state_ == State::WaitForSixIndexPulses && index_hole_count_ == 6)
	{
		state_ = wait_six_index_pulses_.next_state;
	}
}
