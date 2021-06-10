#! /bin/bash

./create_patch.sh
./set_up_machine.sh	|| exit 1
./apply_patch.sh	|| exit 1
./install_driver.sh || exit 1
