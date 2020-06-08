#!/bin/bash

dirMount='/mnt/oui'

clear

echo -e "Disclaimer : parfois le bug n'est visible que la premiere fois."
echo -e "\n\nPret a reproduire le bug de OUICHEFS ? (y/n)"


read ouinon
if [ $ouinon = "y" ]; then
        dirBig=${dirMount}'/testBig'
        mkdir $dirBig

        for j in `seq 1 50`;
                do
                dd if=/dev/zero of=${dirBig}/test${j}.img bs=1M count=4 
        done
        
        for j in `seq 1 5`;
                do
                rm ${dirBig}/test${j}.img
        done
        
        for j in `seq 51 100`;
                do
                dd if=/dev/zero of=${dirBig}/test${j}.img bs=1M count=4 
        done
        
        clear

        du -sh --apparent-size ${dirBig};df -h;ls -la ${dirBig}
fi


