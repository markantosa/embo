#pragma once
#include <vector>
#include <string>

struct MenuItem {
    std::string label;
    int actionId;   // maps to whatever the item should trigger
};

class MenuLogic {
public:
    void begin(const std::vector<MenuItem>& items) {
        _items = items;
        _selectedIndex = 0;
    }

    void next() {
        _selectedIndex = (_selectedIndex + 1) % _items.size();  // wraps around
    }

    int select() {
        return _items[_selectedIndex].actionId;   // caller decides what to do with it
    }

    int getSelectedIndex() const { return _selectedIndex; }
    const std::vector<MenuItem>& getItems() const { return _items; }

private:
    std::vector<MenuItem> _items;
    int _selectedIndex = 0;
};