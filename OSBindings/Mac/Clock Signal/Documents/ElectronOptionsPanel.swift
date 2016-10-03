//
//  ElectronOptionsPanel.swift
//  Clock Signal
//
//  Created by Thomas Harte on 02/10/2016.
//  Copyright © 2016 Thomas Harte. All rights reserved.
//

class ElectronOptionsPanel: MachinePanel {
	var electron: CSElectron! {
		get {
			return self.machine as! CSElectron
		}
	}

	@IBOutlet var displayTypeButton: NSPopUpButton?
	@IBAction func setDisplayType(_ sender: NSPopUpButton!) {
		electron.useTelevisionOutput = (sender.indexOfSelectedItem == 1)
//		UserDefaults.standard.set(sender.indexOfSelectedItem, forKey: self.displayTypeUserDefaultsKey)
	}
}
