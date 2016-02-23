module Felica
  class FelicaStatusError < StandardError
    S1_ERRORS = {
      0x00 => "Success",
      0xFF => "Error (no block list)",
    }
    def self.s1_strerror(s1)
      S1_ERRORS[s1] || "Error (block list)"
    end

    S2_ERRORS = {
      # common
      0x00 => "Success",
      0x01 => "Purse data under/overflow",
      0x02 => "Cashback data exceeded",
      0x70 => "Memory error",
      0x71 => "Memory warning",

      # card-specific
      0xA1 => "Illegal Number of Service",
      0xA2 => "Illegal command packet (specified Number of Block",
      0xA3 => "Illegal Block List (specified order of Service",
      0xA4 => "Illegal Service type",
      0xA5 => "Access is not allowed",
      0xA6 => "Illegal Service Code List",
      0xA7 => "Illegal Block List (access mode)",
      0xA8 => "Illegal Block Number (access to the specified data is inhibited",
      0xA9 => "Data write failure",
      0xAA => "Key-change failure",
      0xAB => "Illegal Package Parity or Illegal Package MAC",
      0xAC => "Illegal parameter",
      0xAD => "Service exists already",
      0xAE => "Illegal System Code",
      0xAF => "Too many simulatenous cyclic write operations",
      0xC0 => "Illegal Package Identifier",
      0xC1 => "Discrepancy of parameters inside and outside Package",
      0xC2 => "Command is disabled already",
    }
    def self.s2_strerror(s2)
      S2_ERRORS[s2] || "Unknown"
    end
    
    def initialize(s1, s2)
      @s1 = s1; @s2 = s2
    end
    def self.raise!(s1, s2)
      raise(self.new(s1, s2), "Status flag error: #{self.s1_strerror(s1)}(#{s1}) #{self.s2_strerror(s2)}(#{s2})")
    end
  end
end
