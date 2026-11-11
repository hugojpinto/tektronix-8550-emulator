// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Hugo José Pinto <hugo.pinto@gmail.com>
// Named outputs (lamps, LEDs, drive activity), after MAME's output manager.
// Machines' lamps are published under stable names ("8501:led3"); 1 = lit.
// Front ends read them (panels) or subscribe; headless runs may log them.
#ifndef TEK_SYSTEM_OUTPUTS_H
#define TEK_SYSTEM_OUTPUTS_H
#include <string>
#include <map>
#include <vector>
#include <functional>

class outputs
{
public:
	using listener = std::function<void(const std::string &name, int value)>;
	void set(const std::string &name, int value)
	{
		auto it = m_values.find(name);
		if (it != m_values.end() && it->second == value) return;
		m_values[name] = value;
		for (auto &l : m_listeners) l(name, value);
	}
	int get(const std::string &name, int dflt = 0) const
	{
		auto it = m_values.find(name);
		return it == m_values.end() ? dflt : it->second;
	}
	const std::map<std::string, int> &all() const { return m_values; }
	void subscribe(listener l) { m_listeners.push_back(std::move(l)); }
private:
	std::map<std::string, int> m_values;
	std::vector<listener> m_listeners;
};
#endif
