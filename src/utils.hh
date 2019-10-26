double
db_to_factor (double dB)
{
  return pow (10, dB / 20);
}

double
db_from_factor (double factor, double min_dB)
{
  if (factor > 0)
    return 20 * log10 (factor);
  else
    return min_dB;
}


