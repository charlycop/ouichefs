#!/bin/bash

dirMount='/mnt/oui'

echo "On cre un ancien fichier de reference ? (y/n)"
read ouinon
if [ $ouinon = "y" ]; then 
        dirOld=${dirMount}'/dirold'
        mkdir ${dirOld}
        dirOld=${dirOld}'/subfolder'
        mkdir ${dirOld}
        dirOld=$dirOld'/subsubfolder'
        mkdir ${dirOld}
        fichierOld=${dirOld}'/fichierOld'
        touch ${fichierOld}
        echo -e "\n\nPremier fichier (le plus vieux) cree avec succes:\n ${fichierOld}"
fi

dirFull=${dirMount}'/dirFull'

echo -e "\n\nDossier ${dirfull}, pret a le remplir de 128 fichiers dans ${dirfull}? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        mkdir ${dirFull}
        for i in `seq 1 128`;
	        do
                touch ${dirFull}/file$i
        done
fi

echo -e "\n\nPret a le remplir de 128 fichiers à nouveau ? (y/n)"

read ouinon
if [ $ouinon = "y" ]; then
        for k in `seq 129 256`;
	        do
                touch ${dirFull}/file$k
        done
fi

echo -e "\n\nPret a le remplir de 50 fichiers de 4Mo ? (y/n)"

dirBig=${dirMount}'/testBig'

read ouinon
if [ $ouinon = "y" ]; then
        mkdir $dirBig

        for j in `seq 1 50`;
                do
                dd if=/dev/zero of=${dirBig}/test${j}.img bs=1M count=4 
        done
fi
