sudo apt install -y libvulkan-dev
make -C ~/vk-vram-overflow
# add to env section of ~/Games/star-citizen/sc-launch.sh:
export VK_LAYER_PATH=~/vk-vram-overflow
export VK_INSTANCE_LAYERS=VK_LAYER_vram_overflow
