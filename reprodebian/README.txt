python -m venv venv
. venv/bin/activate
pip install git+https://github.com/systemd/mkosi.git@c5324fcdd26863c5349785f765fa34af0a45e394
git submodule update --init --recursive
./fetch-deps.sh
mkosi build
truncate --size=100g persistent.img
sudo ./qemu-tdx-launch
