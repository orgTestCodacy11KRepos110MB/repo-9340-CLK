//
//  DMK.hpp
//  Clock Signal
//
//  Created by Thomas Harte on 08/01/2018.
//  Copyright © 2018 Thomas Harte. All rights reserved.
//

#ifndef DMK_hpp
#define DMK_hpp

#include "../DiskImage.hpp"
#include "../../../FileHolder.hpp"

namespace Storage {
namespace Disk {

/*!
	Provides a @c Disk containing a DMK disk image — mostly a decoded byte stream, but with
	a record of IDAM locations.
*/
class DMK: public DiskImage {
	public:
		/*!
			Construct a @c DMK containing content from the file with name @c file_name.

			@throws ErrorNotDMK if this file doesn't appear to be a DMK.
		*/
		DMK(const char *file_name);

		enum {
			ErrorNotDMK
		};

		// implemented to satisfy @c Disk
		int get_head_position_count() override;
		int get_head_count() override;
		bool get_is_read_only() override;

		std::shared_ptr<::Storage::Disk::Track> get_track_at_position(::Storage::Disk::Track::Address address) override;

	private:
		FileHolder file_;
		long get_file_offset_for_position(Track::Address address);

		bool is_read_only_;
		int head_position_count_;
		int head_count_;

		long track_length_;
		bool is_purely_single_density_;
};

}
}

#endif /* DMK_hpp */