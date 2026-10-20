#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace world_explorer::color_inputs {

// Receipt-order evidence, including the final 120-view boundary. The last
// eight wire bits are always zero; legacy records emit only the first word.
class CandidateViews {
  public:
    void add(std::size_t index) {
        if (index >= 120uz) {
            throw std::out_of_range("Color candidate view exceeds budget");
        }
        m_words[index / 32uz] |= std::uint32_t { 1 } << (index % 32uz);
    }

    std::size_t count() const {
        std::size_t result {};
        for (auto word : m_words) {
            result += static_cast<std::size_t>(std::popcount(word));
        }
        return result;
    }

    const std::array<std::uint32_t, 4>& words() const {
        return m_words;
    }

  private:
    std::array<std::uint32_t, 4> m_words {};
};

}  // namespace world_explorer::color_inputs
